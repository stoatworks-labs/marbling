//===========================================================================
// What the tools do to the ink: --drop, --rings, --jacobian, --ink, --undo,
// --period, --resample. Each reads the backward map out of the picture (the
// uv card) or the ink out of it (label cards), and measures it against
// something that is not the shader: [Lu]'s closed forms in double, areas by
// shoelace, the identity, the picture's own period, a bilinear prediction.
//===========================================================================

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr )
{
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.SetHooksForTest( perturb.hooks, perturb.cpuHooks );
	//Every control a check depends on, set (honeydew's lesson): no onsets,
	//full Mix, Auto Paper on.
	rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Manual ) );
	rig.Set( PT_MIX, 1.0f );
	rig.Set( PT_AUTO_PAPER, 1.0f );
	return true;
}

/// The map read out of a uv-card render: Q (centred px, y up) where it can
/// be recovered (inside the window by half a texel), and a drop's label
/// where B is 1 (label cards: R = k / 32).
struct Mapped
{
	int w = 0, h = 0;
	std::vector< double > qx, qy;
	std::vector< int > label;///< 0: the map; k: drop k's ink; -1: neither (a blend)
	std::vector< char > valid;
	size_t at( int i, int j ) const
	{
		return static_cast< size_t >( j ) * w + i;
	}
	double px( int i ) const
	{
		return i + 0.5 - 0.5 * w;
	}
	double py( int j ) const
	{
		return j + 0.5 - 0.5 * h;
	}
};

Mapped readMap( const Rig& rig )
{
	const Floats o = rig.Output();
	Mapped m;
	m.w = rig.width;
	m.h = rig.height;
	const size_t n = static_cast< size_t >( m.w ) * m.h;
	m.qx.resize( n );
	m.qy.resize( n );
	m.label.resize( n );
	m.valid.resize( n );
	for( size_t k = 0; k < n; ++k )
	{
		const double r = o[ k * 4 ], g = o[ k * 4 + 1 ], b = o[ k * 4 + 2 ];
		if( b == 0.0 )
		{
			m.label[ k ] = 0;
			m.qx[ k ]    = r * m.w - 0.5 * m.w;
			m.qy[ k ]    = g * m.h - 0.5 * m.h;
			m.valid[ k ] = std::fabs( m.qx[ k ] ) < 0.5 * m.w - 0.5 - 1e-3 && std::fabs( m.qy[ k ] ) < 0.5 * m.h - 0.5 - 1e-3;
		}
		else
		{
			//A label is exact to float's mix of equal texels (an ulp or two).
			const double k32 = r * 32.0;
			const bool label = std::fabs( b - 1.0 ) < 1e-6 && std::fabs( k32 - std::round( k32 ) ) < 1e-4 && g == 0.0 && std::round( k32 ) >= 1.0;
			m.label[ k ] = label ? static_cast< int >( std::lround( k32 ) ) : -1;
			m.valid[ k ] = 0;
		}
	}
	//The card is mirrored beyond the window, so a position pushed outside
	//reads back as its reflection, inside. Every tool keeps orientation; a
	//reflection reverses it. So a pixel counts only where the map's local
	//determinant (central differences) is positive around it.
	std::vector< double > det( n, -1.0 );
	for( int j = 1; j + 1 < m.h; ++j )
		for( int i = 1; i + 1 < m.w; ++i )
		{
			const size_t c = m.at( i, j ), l = m.at( i - 1, j ), r = m.at( i + 1, j ), d = m.at( i, j - 1 ), u = m.at( i, j + 1 );
			if( m.label[ c ] != 0 || m.label[ l ] != 0 || m.label[ r ] != 0 || m.label[ d ] != 0 || m.label[ u ] != 0 )
				continue;
			det[ c ] = 0.25 * ( ( m.qx[ r ] - m.qx[ l ] ) * ( m.qy[ u ] - m.qy[ d ] ) - ( m.qx[ u ] - m.qx[ d ] ) * ( m.qy[ r ] - m.qy[ l ] ) );
		}
	std::vector< char > keep( n, 0 );
	for( int j = 0; j < m.h; ++j )
		for( int i = 0; i < m.w; ++i )
		{
			bool ok = m.valid[ m.at( i, j ) ];
			for( int dj = -1; dj <= 1 && ok; ++dj )
				for( int di = -1; di <= 1 && ok; ++di )
				{
					const int ii = i + di, jj = j + dj;
					ok = ii < 0 || jj < 0 || ii >= m.w || jj >= m.h || m.label[ m.at( ii, jj ) ] != 0 || det[ m.at( ii, jj ) ] > 0.0
					     || ii == 0 || jj == 0 || ii == m.w - 1 || jj == m.h - 1;
				}
			keep[ m.at( i, j ) ] = ok;
		}
	m.valid.swap( keep );
	return m;
}

/// P (centred px of a raster H px high) back through one op, in double:
/// the harness's own [Lu], written from the paper, not from the shader.
/// True when P lands inside a drop.
bool backThrough( const Op& o, double H, double& x, double& y )
{
	if( o.type == OpType::Drop )
	{
		const double cx = o.cx * H, cy = o.cy * H, r = o.r * H;
		const double dx = x - cx, dy = y - cy, rho2 = dx * dx + dy * dy;
		if( rho2 < r * r )
			return true;
		//[Lu] p. 27: P = C + (P' - C) sqrt( 1 - r^2 / |P' - C|^2 ).
		const double s = std::sqrt( 1.0 - r * r / rho2 );
		x              = cx + dx * s;
		y              = cy + dy * s;
		return false;
	}
	if( o.type == OpType::Comb )
	{
		const double mx = o.mx, my = o.my, nx = -my, ny = mx;
		const double amp = o.waveAmp * H, k = o.waveK / H;
		if( amp != 0.0 )
		{
			const double w = amp * std::sin( k * ( x * mx + y * my ) + o.wavePhase );
			x -= w * nx;
			y -= w * ny;
		}
		double d = std::fabs( x * nx + y * ny - o.offset * H );
		if( o.spacing > 0.0 )
		{
			//[Lu] p. 28: d' = s/2 - | fmod( d, s ) - s/2 |.
			const double s = o.spacing * H;
			d              = 0.5 * s - std::fabs( std::fmod( d, s ) - 0.5 * s );
		}
		//[Lu] Eq. (1), backward: the shift alpha lambda / ( d + lambda ) undone.
		const double a = o.alpha * H, l = o.lambda * H, g = a * l / ( d + l );
		x -= g * mx;
		y -= g * my;
		if( amp != 0.0 )
		{
			const double w = amp * std::sin( k * ( x * mx + y * my ) + o.wavePhase );
			x += w * nx;
			y += w * ny;
		}
		return false;
	}
	//Circular, [Lu] Eq. (2): theta = l / rho, l = alpha lambda / ( |rho - r| + lambda ),
	//forward clockwise on screen, so backward anticlockwise (y up).
	const double cx = o.cx * H, cy = o.cy * H, dx = x - cx, dy = y - cy, rho = std::hypot( dx, dy );
	if( rho > 0.0 )
	{
		const double a = o.alpha * H, l = o.lambda * H, r = o.r * H;
		const double th = a * l / ( std::fabs( rho - r ) + l ) / rho;
		x               = cx + std::cos( th ) * dx - std::sin( th ) * dy;
		y               = cy + std::sin( th ) * dx + std::cos( th ) * dy;
	}
	return false;
}

/// Back through every op, newest first. Returns the drop's index in `ops`
/// whose ink P lands in, or -1.
int backThroughAll( const std::vector< Op >& ops, double H, double& x, double& y )
{
	for( int k = static_cast< int >( ops.size() ) - 1; k >= 0; --k )
		if( backThrough( ops[ static_cast< size_t >( k ) ], H, x, y ) )
			return k;
	return -1;
}

Op dropOp( double cx, double cy, double r )
{
	Op o;
	o.type = OpType::Drop;
	o.cx   = cx;
	o.cy   = cy;
	o.r    = r;
	return o;
}
Op combOp( double mx, double my, double offset, double spacing, double alpha, double lambda, double amp = 0.0, double waveK = 0.0, double phase = 0.0 )
{
	Op o;
	o.type      = OpType::Comb;
	o.mx        = mx;
	o.my        = my;
	o.offset    = offset;
	o.spacing   = spacing;
	o.alpha     = alpha;
	o.lambda    = lambda;
	o.waveAmp   = amp;
	o.waveK     = waveK;
	o.wavePhase = phase;
	return o;
}
Op circleOp( double cx, double cy, double r, double alpha, double lambda )
{
	Op o;
	o.type   = OpType::Circle;
	o.cx     = cx;
	o.cy     = cy;
	o.r      = r;
	o.alpha  = alpha;
	o.lambda = lambda;
	return o;
}

/// One op into the bath, finished, with its frame rendered: a drop on a
/// label card (its ink reads R = label / 32), then the uv card back.
bool injectAndRender( Rig& rig, const Op& op, int label, const Floats& uv )
{
	if( op.type == OpType::Drop )
		rig.Upload( labelCard( rig.width, rig.height, label ) );
	rig.Inject( op );
	if( !rig.Render( 1 ) )
		return false;
	if( op.type == OpType::Drop )
		rig.Upload( uv );
	return rig.Render( 1 );
}

constexpr double kEps32 = 1.0 / 16777216.0;///< 2^-24, float's unit roundoff

//===========================================================================
// --drop: [Lu]'s drop, per pixel, against its closed form in double.
//===========================================================================
int runDrop( const Perturb& perturb )
{
	std::printf( "\n=== drop: outside a drop, every pixel's backward map is [Lu]'s C + (P - C) sqrt(1 - r^2/|P - C|^2), composed in double "
	             "for two overlapping drops; inside, that drop's ink\n" );
	for( const Raster& raster : kRasters )
	{
		const Floats uv = uvCard( raster.w, raster.h );
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb, &uv ) )
			return 1;
		const std::vector< Op > ops = { dropOp( 0.1, -0.05, 0.12 ), dropOp( -0.05, 0.02, 0.09 ) };
		std::vector< Op > model;
		for( size_t d = 0; d < ops.size(); ++d )
		{
			if( !injectAndRender( rig, ops[ d ], static_cast< int >( d ) + 1, uv ) )
				return 1;
			model.push_back( ops[ d ] );
			const Mapped m  = readMap( rig );
			const double H  = raster.h;
			double worst    = 0.0, worstTol = 0.0, worstRatio = 0.0;
			int measured = 0, tight = 0, wrongLabel = 0, considered = 0;
			for( int j = 0; j < m.h; ++j )
				for( int i = 0; i < m.w; ++i )
				{
					double x = m.px( i ), y = m.py( j );
					const int inside = backThroughAll( model, H, x, y );
					//The boundary's own float precision: no label is judged within
					//0.01 px of a rim.
					bool nearRim = false;
					{
						//Distance to each rim along the trace, in double.
						double tx = m.px( i ), ty = m.py( j );
						for( int k = static_cast< int >( model.size() ) - 1; k >= 0 && !nearRim; --k )
						{
							const Op& o = model[ static_cast< size_t >( k ) ];
							const double rho = std::hypot( tx - o.cx * H, ty - o.cy * H );
							nearRim          = std::fabs( rho - o.r * H ) < 0.01;
							if( backThrough( o, H, tx, ty ) )
								break;
						}
					}
					const size_t at = m.at( i, j );
					if( !nearRim )
					{
						++considered;
						const int expect = inside >= 0 ? inside + 1 : 0;
						if( m.label[ at ] != expect )
							++wrongLabel;
					}
					if( inside >= 0 || !m.valid[ at ] || nearRim )
						continue;
					//The backward map's own stretch at P (finite differences in
					//double) amplifies float's roundoff of P and of each step.
					double ax = m.px( i ) + 1e-3, ay = m.py( j ), bx = m.px( i ), by = m.py( j ) + 1e-3;
					backThroughAll( model, H, ax, ay );
					backThroughAll( model, H, bx, by );
					const double stretch = std::max( { 1.0, std::hypot( ax - x, ay - y ) / 1e-3, std::hypot( bx - x, by - y ) / 1e-3 } );
					const double tol     = 1e-3 + 16.0 * kEps32 * m.w * stretch;
					const double err     = std::hypot( m.qx[ at ] - x, m.qy[ at ] - y );
					++measured;
					tight += tol <= 0.01;
					if( err / tol > worstRatio )
					{
						worstRatio = err / tol;
						worst      = err;
						worstTol   = tol;
					}
				}
			Check( wrongLabel == 0 && worstRatio <= 1.0 && measured > 0 && tight >= measured * 0.95,
			       fmt( "%dx%d, %zu drop%s: %d of %d pixels show the wrong ink (0.01 px from a rim excluded); map error worst %.2e px against its "
			            "tolerance %.2e (1e-3 px + 16 x 2^-24 x width x the map's stretch there), %d pixels measured, %.1f%% of them to 0.01 px",
			            raster.w, raster.h, model.size(), model.size() == 1 ? "" : "s", wrongLabel, considered, worst, worstTol, measured,
			            measured > 0 ? 100.0 * tight / measured : 0.0 ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --rings: concentric drops. Drop k's ink must end as the annulus between
// r sqrt(n - k) and r sqrt(n - k + 1): rho' = sqrt( rho^2 + r^2 ), area kept.
//===========================================================================
int runRings( const Perturb& perturb )
{
	constexpr int kRings = 5;
	constexpr double kR = 0.1, kCx = 0.0317, kCy = -0.0213;
	std::printf( "\n=== rings: %d concentric drops of r = %.2f frame heights; drop k's ink is the annulus r sqrt(n-k) .. r sqrt(n-k+1) ([Lu] p. 27: "
	             "old ink to sqrt(rho^2 + r^2)), each edge found to the pixel grid's sqrt(2)/2 px\n",
	             kRings, kR );
	for( const Raster& raster : kRasters )
	{
		const Floats uv = uvCard( raster.w, raster.h );
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb, &uv ) )
			return 1;
		for( int k = 1; k <= kRings; ++k )
			if( !injectAndRender( rig, dropOp( kCx, kCy, kR ), k, uv ) )
				return 1;
		const Mapped m = readMap( rig );
		const double H = raster.h, cx = kCx * H, cy = kCy * H;
		std::vector< double > lo( kRings + 1, 1e30 ), hi( kRings + 1, -1.0 );
		std::vector< int > count( kRings + 1, 0 );
		for( int j = 0; j < m.h; ++j )
			for( int i = 0; i < m.w; ++i )
			{
				const int l = m.label[ m.at( i, j ) ];
				if( l < 1 || l > kRings )
					continue;
				const double rho = std::hypot( m.px( i ) - cx, m.py( j ) - cy );
				lo[ l ]          = std::min( lo[ l ], rho );
				hi[ l ]          = std::max( hi[ l ], rho );
				++count[ l ];
			}
		const double half = std::sqrt( 0.5 ), fp = 1e-3;
		double worst      = 0.0;
		std::string what;
		bool ok = true;
		for( int k = 1; k <= kRings; ++k )
		{
			const double outer = kR * H * std::sqrt( static_cast< double >( kRings - k + 1 ) );
			const double inner = kR * H * std::sqrt( static_cast< double >( kRings - k ) );
			//Pixel centres inside an annulus at least 2 px wide come within
			//sqrt(2)/2 of each edge and never cross it.
			const bool outerOk = hi[ k ] <= outer + fp && hi[ k ] >= outer - half - fp;
			const bool innerOk = k == kRings ? lo[ k ] <= half + fp : ( lo[ k ] >= inner - fp && lo[ k ] <= inner + half + fp );
			ok                 = ok && outerOk && innerOk && count[ k ] > 0;
			worst              = std::max( { worst, hi[ k ] - outer, outer - half - hi[ k ], k == kRings ? 0.0 : inner - lo[ k ], k == kRings ? 0.0 : lo[ k ] - inner - half } );
			const double area  = kPi * ( outer * outer - inner * inner );
			what += fmt( "\n      drop %d: %.2f..%.2f px (expected %.2f..%.2f), %d px against pi(ro^2 - ri^2) = %.0f", k, k == kRings ? 0.0 : lo[ k ], hi[ k ],
			             inner, outer, count[ k ], area );
		}
		Check( ok, fmt( "%dx%d: every edge within its grid bound (worst excess %.3f px beyond it, <= 0 passes):%s", raster.w, raster.h, worst, what.c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --jacobian: every tool preserves area. Per block of the frame, the
// backward map's image of the block's boundary, by shoelace, against the
// block's own area.
//===========================================================================
struct BlockArea
{
	int clean = 0, resolved = 0, failed = 0;
	double worstRel = 0.0, worstTolRel = 0.0, medianTolRel = 0.0;
};

BlockArea blockAreas( const Mapped& m, int b, double limitRel )
{
	BlockArea out;
	std::vector< double > tols;
	//The map's second difference along a grid line through (i, j): the
	//curvature of the image of that line, not of the loop's corners.
	auto second = [ & ]( int i, int j, int di, int dj ) {
		const size_t a = m.at( i - di, j - dj ), c = m.at( i, j ), d = m.at( i + di, j + dj );
		return std::hypot( m.qx[ d ] - 2.0 * m.qx[ c ] + m.qx[ a ], m.qy[ d ] - 2.0 * m.qy[ c ] + m.qy[ a ] );
	};
	for( int j0 = 1; j0 + b + 1 < m.h; j0 += b )
		for( int i0 = 1; i0 + b + 1 < m.w; i0 += b )
		{
			bool clean = true;
			for( int j = j0 - 1; j <= j0 + b + 1 && clean; ++j )
				for( int i = i0 - 1; i <= i0 + b + 1 && clean; ++i )
					clean = m.valid[ m.at( i, j ) ] && m.label[ m.at( i, j ) ] == 0;
			if( !clean )
				continue;
			++out.clean;
			//The boundary through pixel centres, anticlockwise.
			std::vector< std::pair< int, int > > loop;
			for( int i = i0; i < i0 + b; ++i )
				loop.emplace_back( i, j0 );
			for( int j = j0; j < j0 + b; ++j )
				loop.emplace_back( i0 + b, j );
			for( int i = i0 + b; i > i0; --i )
				loop.emplace_back( i, j0 + b );
			for( int j = j0 + b; j > j0; --j )
				loop.emplace_back( i0, j );
			const size_t n = loop.size();
			double area = 0.0, perimeter = 0.0;
			//Error sources: each chord against its arc, at most 2/3 chord x
			//sagitta, the sagitta at most K/8 for a curvature K bounded by
			//twice the larger second difference at the chord's two ends (a
			//kink between samples); and float's error in each vertex (5e-4
			//px) along the perimeter twice.
			double sag = 0.0;
			for( size_t k = 0; k < n; ++k )
			{
				const size_t q = ( k + 1 ) % n;
				const int ia = loop[ k ].first, ja = loop[ k ].second, ib = loop[ q ].first, jb = loop[ q ].second;
				const size_t a = m.at( ia, ja ), c = m.at( ib, jb );
				area += m.qx[ a ] * m.qy[ c ] - m.qx[ c ] * m.qy[ a ];
				const double chord = std::hypot( m.qx[ c ] - m.qx[ a ], m.qy[ c ] - m.qy[ a ] );
				perimeter += chord;
				const int di = ib - ia, dj = jb - ja;
				const double K = 2.0 * std::max( second( ia, ja, di, dj ), second( ib, jb, di, dj ) );
				sag += chord * K / 12.0;
			}
			area *= 0.5;
			const double tol = sag + 2.0 * 5e-4 * perimeter;
			const double a0 = static_cast< double >( b ) * b, rel = std::fabs( area - a0 ) / a0, tolRel = tol / a0;
			tols.push_back( tolRel );
			if( tolRel > limitRel )
				continue;
			++out.resolved;
			if( rel > tolRel )
				++out.failed;
			if( rel > out.worstRel )
			{
				out.worstRel    = rel;
				out.worstTolRel = tolRel;
			}
		}
	if( !tols.empty() )
	{
		std::sort( tols.begin(), tols.end() );
		out.medianTolRel = tols[ tols.size() / 2 ];
	}
	return out;
}

int runJacobian( const Perturb& perturb )
{
	std::printf( "\n=== jacobian: each tool, composed one after another, leaves every block of the frame its area: shoelace over the backward map's "
	             "image of each block's boundary (blocks 0.1 frame heights, at least 36 px), against the chord-sag and float bound\n" );
	struct Stage
	{
		const char* what;
		Op op;
	};
	const Stage stages[] = {
		{ "one tine line", combOp( 1, 0, 0.05, 0.0, 0.1, 0.1 ) },
		{ "+ a comb across (fold kinks)", combOp( 0, 1, 0.1, 0.35, 0.08, 0.1 ) },
		{ "+ a wavy comb", combOp( 1, 0, 0.0, 0.5, 0.06, 0.1, 0.03, 2.0 * kPi / 0.5, 0.3 ) },
		{ "+ a circular stroke", circleOp( 0.25, 0.1, 0.22, 0.1, 0.1 ) },
		{ "+ a vortex", circleOp( -0.35, -0.1, 0.0, 0.12, 0.1 ) },
		{ "+ a drop (outside its ink)", dropOp( 0.0, 0.15, 0.08 ) },
	};
	//A block counts when its bound is under 0.2% of its area (the
	//compressible control perturbs by up to 2%).
	constexpr double kLimit = 2e-3;
	for( const Raster& raster : kRasters )
	{
		const Floats uv = uvCard( raster.w, raster.h );
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb, &uv ) )
			return 1;
		//Blocks of 0.1 frame heights, at least 36 px: the chord-sag bound
		//relative to a block's area falls as the block grows in pixels.
		const int b = std::max( 36, static_cast< int >( std::lround( 0.1 * raster.h ) ) );
		for( const Stage& stage : stages )
		{
			if( !injectAndRender( rig, stage.op, 1, uv ) )
				return 1;
			const BlockArea a = blockAreas( readMap( rig ), b, kLimit );
			//At least four blocks, and a third of the clean ones, resolved:
			//never a pass over nothing.
			Check( a.failed == 0 && a.resolved >= std::max( 4, ( a.clean + 2 ) / 3 ),
			       fmt( "%dx%d, %-28s %d of %d clean blocks (%d px) resolved to 0.2%%, %d outside their bound; worst |area/area0 - 1| %.2e "
			            "(its bound %.2e)",
			            raster.w, raster.h, stage.what, a.resolved, a.clean, b, a.failed, a.worstRel, a.worstTolRel ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --ink: no ink lost or created (SPEC-marbling.md). Every pixel is exactly
// one thing, a drop's ink bit for bit or the paper; no ink appears that was
// not dropped; each trigger's ink keeps its area while inside the window,
// through the real recipe, with and without a bake after every trigger.
//===========================================================================
/// Label counts and the pixels each label's edge can cross (smooth at the
/// pixel scale: k's own pixels with a neighbour not k, and every other pixel
/// with a neighbour k, each counted once per label).
struct InkCensus
{
	std::map< int, int > count, edge;
	std::map< int, bool > touches;
	int foreign = 0;
};

InkCensus census( const Mapped& m )
{
	InkCensus c;
	for( int j = 0; j < m.h; ++j )
		for( int i = 0; i < m.w; ++i )
		{
			const int l   = m.label[ m.at( i, j ) ];
			int seen[ 9 ] = {};
			int nSeen     = 0;
			bool boundary = false;
			for( int dj = -1; dj <= 1; ++dj )
				for( int di = -1; di <= 1; ++di )
				{
					const int ii = std::clamp( i + di, 0, m.w - 1 ), jj = std::clamp( j + dj, 0, m.h - 1 );
					const int n  = m.label[ m.at( ii, jj ) ];
					if( n == l )
						continue;
					boundary = true;
					if( n >= 1 && std::find( seen, seen + nSeen, n ) == seen + nSeen )
						seen[ nSeen++ ] = n;
				}
			for( int s = 0; s < nSeen; ++s )
				++c.edge[ seen[ s ] ];
			if( l < 0 )
			{
				++c.foreign;
				continue;
			}
			if( l == 0 )
				continue;
			++c.count[ l ];
			c.edge[ l ] += boundary;
			if( i < 2 || j < 2 || i >= m.w - 2 || j >= m.h - 2 )
				c.touches[ l ] = true;
		}
	return c;
}

int runInk( const Perturb& perturb )
{
	std::printf( "\n=== ink: no ink lost or created. Through Nonpareil's real triggers (drops on label cards), every pixel is one ink exactly or the "
	             "paper and no label appears that was not dropped; in a marbling kept inside the window, every drop's ink keeps pi r^2 within the "
	             "pixel grid's bound, traced exactly or baked after every op\n" );
	for( const Raster& raster : kRasters )
		for( bool bakeAll : { false, true } )
		{
			//(a) The real recipe.
			{
				const Floats uv = uvCard( raster.w, raster.h );
				Rig rig;
				if( !startRig( rig, raster.w, raster.h, perturb, &uv ) )
					return 1;
				rig.plugin.SetBakeEveryTriggerForTest( bakeAll );
				rig.Set( PT_PATTERN, static_cast< float >( Pattern::Nonpareil ) );
				rig.Set( PT_DROP_SIZE, ParamFromDropRadius( 0.07 ) );
				rig.Set( PT_DROP_RATE, ParamFromDropRate( 3 ) );
				rig.Set( PT_SPEED, ParamFromStrokeSeconds( 0.3 ) );
				rig.Set( PT_AUTO_PAPER, 0.0f );
				std::vector< int > dropLabels;
				int label = 0;
				while( rig.plugin.BathForTest().CurrentStage() != Bath::Stage::Done && label < 30 )
				{
					++label;
					const bool drops = rig.plugin.BathForTest().CurrentStage() == Bath::Stage::Drops;
					rig.Upload( drops ? labelCard( raster.w, raster.h, label ) : uv );
					if( !rig.Press( PT_TRIGGER ) )
						return 1;
					if( drops )
						dropLabels.push_back( label );
					rig.Upload( uv );
					if( !rig.Render( 24 ) )//each stroke finishes (0.3 s) before the next
						return 1;
				}
				const InkCensus c = census( readMap( rig ) );
				int never = 0, inked = 0;
				for( const auto& entry : c.count )
				{
					never += std::find( dropLabels.begin(), dropLabels.end(), entry.first ) == dropLabels.end() ? entry.second : 0;
					inked += entry.second;
				}
				Check( c.foreign == 0 && never == 0 && inked > raster.w * raster.h / 20,
				       fmt( "%dx%d, Nonpareil's %d triggers, %s: %d pixels a blend of inks or a colour never dropped, %d with a label never dropped; "
				            "%d ink pixels",
				            raster.w, raster.h, label, bakeAll ? "baked after every trigger" : "traced exactly", c.foreign, never, inked ) );
			}
			//(b) Overlapping drops near the centre, combed gently: the ink stays
			//in the window, so every drop's area can be counted.
			{
				const Floats uv = uvCard( raster.w, raster.h );
				Rig rig;
				if( !startRig( rig, raster.w, raster.h, perturb, &uv ) )
					return 1;
				rig.plugin.SetBakeEveryTriggerForTest( bakeAll );
				const double radii[] = { 0.08, 0.06, 0.07, 0.05, 0.065, 0.055 };
				const double xs[] = { 0.0, 0.08, -0.07, 0.03, -0.02, 0.1 }, ys[] = { 0.0, 0.05, 0.04, -0.08, 0.09, -0.04 };
				for( int k = 0; k < 6; ++k )
					if( !injectAndRender( rig, dropOp( xs[ k ], ys[ k ], radii[ k ] ), k + 1, uv ) )
						return 1;
				const Op strokes[] = { combOp( 1, 0, 0.0, 0.12, 0.04, 0.02 ), combOp( -1, 0, 0.06, 0.12, 0.04, 0.02 ), combOp( 0, 1, 0.0, 0.12, 0.04, 0.02 ),
				                       combOp( 0, -1, 0.06, 0.12, 0.04, 0.02 ), circleOp( 0.02, 0.0, 0.0, 0.08, 0.05 ) };
				for( const Op& o : strokes )
					if( !injectAndRender( rig, o, 0, uv ) )
						return 1;
				const InkCensus c = census( readMap( rig ) );
				int outside = 0, judged = 0;
				double worst = 0.0;
				std::string what;
				for( int k = 0; k < 6; ++k )
				{
					const int l = k + 1;
					const double expect = kPi * ( radii[ k ] * raster.h ) * ( radii[ k ] * raster.h );
					const int n         = c.count.count( l ) ? c.count.at( l ) : 0;
					const int bound     = c.edge.count( l ) ? c.edge.at( l ) : 0;
					const double ratio  = std::fabs( n - expect ) / std::max( 1, bound );
					judged += !c.touches.count( l ) && n > 0;
					outside += ratio > 1.0 || c.touches.count( l );
					worst = std::max( worst, ratio );
					what += fmt( " %d/%.0f", n, expect );
				}
				Check( c.foreign == 0 && outside == 0 && judged == 6,
				       fmt( "%dx%d, six overlapping drops and five strokes, %s: %d pixels a blend; each drop's pixels against pi r^2:%s; worst "
				            "|count - pi r^2| / bound %.2f (bound = pixels its edge can cross)",
				            raster.w, raster.h, bakeAll ? "baked after every op" : "traced exactly", c.foreign, what.c_str(), worst ) );
			}
		}
	return Verdict();
}

//===========================================================================
// --undo: a reverse stroke along the same tines is the identity; a
// bisecting return is not, and both match [Lu] Eq. (1) composed in double.
//===========================================================================
int runUndo( const Perturb& perturb )
{
	std::printf( "\n=== undo: a comb drawn back along the same tines restores every pixel, wavy or straight; a bisecting return (offset s/2, [BAB]) "
	             "does not; every stroke, comb, wavy comb, circle and vortex, matches [Lu]'s Eq. 1 and 2 composed in double\n" );
	struct Case
	{
		const char* what;
		std::vector< Op > ops;
		bool identity;
	};
	//A diagonal comb, so M and N are not the axes. N flips with M, so the same
	//tines drawn back have offset -offset.
	const double mx = 0.6, my = 0.8, s = 0.12, a = 0.15, l = 0.02, k = 2.0 * kPi / 0.5;
	const std::vector< Case > cases = {
		{ "back along the same tines", { combOp( mx, my, 0.0, s, a, l ), combOp( -mx, -my, 0.0, s, a, l ) }, true },
		//The same wavy path drawn back: with M and N flipped, u = P.M flips
		//too, so the wave's phase must be negated for N w(u) to be one curve.
		{ "a wavy comb and back", { combOp( mx, my, 0.0, s, a, l, 0.02, k, 0.3 ), combOp( -mx, -my, 0.0, s, a, l, 0.02, k, -0.3 ) }, true },
		{ "a bisecting return", { combOp( mx, my, 0.0, s, a, l ), combOp( -mx, -my, -0.5 * s, s, a, l ) }, false },
		{ "a wavy comb", { combOp( 1, 0, 0.01, 0.2, 0.08, 0.03, 0.03, k, 0.3 ) }, false },
		{ "a circular stroke", { circleOp( 0.1, -0.05, 0.2, 0.1, 0.04 ) }, false },
		{ "a vortex", { circleOp( -0.2, 0.05, 0.0, 0.1, 0.06 ) }, false },
	};
	for( const Raster& raster : kRasters )
	{
		const double H = raster.h;
		for( const Case& c : cases )
		{
			const Floats uv = uvCard( raster.w, raster.h );
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb, &uv ) )
				return 1;
			double alphaOverLambda = 1.0;
			for( const Op& o : c.ops )
			{
				alphaOverLambda = std::max( alphaOverLambda, o.alpha / o.lambda );
				if( !injectAndRender( rig, o, 1, uv ) )
					return 1;
			}
			const Mapped m = readMap( rig );
			//Float's roundoff of a position (2^-24 x the raster) moves d, and
			//the shift's slope alpha/lambda carries it; a vortex's theta is
			//steep near its centre, so pixels within 2 lambda of one are left out.
			const double tol = 1e-3 + 8.0 * kEps32 * m.w * ( 1.0 + alphaOverLambda );
			double worst = 0.0, sumMove = 0.0;
			int measured = 0;
			for( int j = 0; j < m.h; ++j )
				for( int i = 0; i < m.w; ++i )
				{
					const size_t at = m.at( i, j );
					bool nearCentre = false;
					for( const Op& o : c.ops )
						nearCentre = nearCentre || ( o.type == OpType::Circle && std::hypot( m.px( i ) - o.cx * H, m.py( j ) - o.cy * H ) < 2.0 * o.lambda * H );
					double x = m.px( i ), y = m.py( j );
					backThroughAll( c.ops, H, x, y );
					if( nearCentre || !m.valid[ at ] || std::fabs( x ) > 0.5 * m.w - 1.0 || std::fabs( y ) > 0.5 * m.h - 1.0 )
						continue;
					++measured;
					worst = std::max( worst, std::hypot( m.qx[ at ] - x, m.qy[ at ] - y ) );
					sumMove += std::hypot( m.qx[ at ] - m.px( i ), m.qy[ at ] - m.py( j ) );
				}
			const double meanMove = measured ? sumMove / measured : 0.0;
			Check( worst <= tol && measured > m.w * m.h / 2 && ( c.identity ? meanMove <= tol : meanMove > 1.0 ),
			       fmt( "%dx%d, %-26s off %s by at most %.2e px over %d pixels (tolerance %.2e: 1e-3 + 8 x 2^-24 x width x (1 + alpha/lambda)); "
			            "moved %.2f px on average",
			            raster.w, raster.h, c.what, c.identity ? "the identity (and the double composition)" : "the double composition", worst, measured,
			            tol, meanMove ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --period: a comb of spacing s repeats every s, read from the picture.
//===========================================================================
int runPeriod( const Perturb& perturb )
{
	std::printf( "\n=== period: one stroke of a comb with tine spacing s; the displacement the picture shows repeats every s (the lag minimising "
	             "mean |D(y + L) - D(y)|, searched 0.6 s .. 1.6 s in 0.01 px)\n" );
	for( const Raster& raster : kRasters )
	{
		const Floats uv = uvCard( raster.w, raster.h );
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb, &uv ) )
			return 1;
		const double s = 0.1;
		if( !injectAndRender( rig, combOp( 1, 0, 0.013, s, 0.12, s / 6.0 ), 1, uv ) )
			return 1;
		const Mapped m = readMap( rig );
		//D(y): the mean shift along M of row y's recoverable pixels.
		std::vector< double > D( static_cast< size_t >( m.h ), 0.0 );
		for( int j = 0; j < m.h; ++j )
		{
			double sum = 0.0;
			int n      = 0;
			for( int i = 0; i < m.w; ++i )
				if( m.valid[ m.at( i, j ) ] )
				{
					sum += m.qx[ m.at( i, j ) ] - m.px( i );
					++n;
				}
			D[ static_cast< size_t >( j ) ] = n ? sum / n : 0.0;
		}
		auto at = [ & ]( double y ) {
			const double c = std::clamp( y, 0.0, static_cast< double >( m.h - 1 ) );
			const int i0   = std::min( static_cast< int >( c ), m.h - 2 );
			return D[ static_cast< size_t >( i0 ) ] + ( D[ static_cast< size_t >( i0 + 1 ) ] - D[ static_cast< size_t >( i0 ) ] ) * ( c - i0 );
		};
		const double sp = s * raster.h;
		double best = 0.0, bestE = 1e30;
		for( double L = 0.6 * sp; L <= 1.6 * sp; L += 0.01 )
		{
			double e = 0.0;
			int n    = 0;
			for( int j = 0; j + L < m.h - 1; ++j )
			{
				e += std::fabs( at( j + L ) - at( j ) );
				++n;
			}
			if( n > 0 && e / n < bestE )
			{
				bestE = e / n;
				best  = L;
			}
		}
		Check( std::fabs( best - sp ) <= 0.5,
		       fmt( "%dx%d: period %.2f px, s = %.2f px (off %.2f px, tolerance 0.5 px: a cusp between samples); residual %.3f px", raster.w,
		            raster.h, best, sp, best - sp, bestE ) );
	}
	return Verdict();
}

//===========================================================================
// --resample: what one bake costs. A comb is baked into the map, then
// undone by its reverse traced exactly: what is left is the bake's
// resampling alone, predicted pixel for pixel as the bilinear reading of the
// comb's map at texel centres, and bounded by the comb's cusp.
//===========================================================================
int runResample( const Perturb& perturb )
{
	std::printf( "\n=== resample: a comb baked, then its reverse traced exactly: the residue is one bake's bilinear resampling, predicted per pixel "
	             "in double; its size is bounded by the tine's cusp, alpha/(2 lambda) + max|g''|/8\n" );
	struct Case
	{
		const char* what;
		double s, a, l;
	};
	const Case cases[] = {
		{ "alpha 0.15, lambda 0.02", 0.12, 0.15, 0.02 },
		{ "the defaults' comb: alpha 0.15, s 0.03, lambda s/6", 0.03, 0.15, 0.005 },
	};
	for( const Raster& raster : kRasters )
		for( const Case& c : cases )
		{
			const double H  = raster.h;
			const Floats uv = uvCard( raster.w, raster.h );
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb, &uv ) )
				return 1;
			//Diagonal, so undoing it crosses texel rows and columns: along its
			//own axis a comb is linear and bilinear reads it exactly.
			const Op forward = combOp( 0.6, 0.8, 0.0, c.s, c.a, c.l ), back = combOp( -0.6, -0.8, 0.0, c.s, c.a, c.l );
			rig.plugin.SetBakeEveryTriggerForTest( true );
			if( !injectAndRender( rig, forward, 1, uv ) )
				return 1;
			rig.plugin.SetBakeEveryTriggerForTest( false );
			const int bakes = rig.plugin.LastFrame().bakesTotal;
			if( !injectAndRender( rig, back, 1, uv ) )
				return 1;
			const Mapped m = readMap( rig );
			const int W = m.w, Hh = m.h;
			auto mirror = []( int i, int n ) {
				const int p = 2 * n;
				int k       = i % p;
				if( k < 0 )
					k += p;
				return k < n ? k : p - 1 - k;
			};
			const double A = c.a * H, L = c.l * H;
			const double tol = 1e-3 + 8.0 * kEps32 * W * ( 1.0 + A / L );
			double worstPred = 0.0, worstBlur = 0.0, sumBlur = 0.0;
			int measured = 0, over = 0, beyond = 0;
			std::vector< double > blurs;
			for( int j = 0; j < Hh; ++j )
				for( int i = 0; i < W; ++i )
				{
					const size_t at = m.at( i, j );
					if( !m.valid[ at ] )
						continue;
					double x = m.px( i ), y = m.py( j );
					backThrough( back, H, x, y );
					//Beyond the window the baked map is the mirror of the inside
					//(SPEC-marbling.md): what is read there is that, not blur.
					if( std::fabs( x ) > 0.5 * W - 1.0 || std::fabs( y ) > 0.5 * Hh - 1.0 )
					{
						++beyond;
						continue;
					}
					const double tx = x + 0.5 * W - 0.5, ty = y + 0.5 * Hh - 0.5;
					const int i0 = static_cast< int >( std::floor( tx ) ), j0 = static_cast< int >( std::floor( ty ) );
					const double fx = tx - i0, fy = ty - j0;
					double qx[ 2 ][ 2 ], qy[ 2 ][ 2 ];
					bool inside = true;
					for( int b = 0; b < 2; ++b )
						for( int a = 0; a < 2; ++a )
						{
							double cx = mirror( i0 + a, W ) + 0.5 - 0.5 * W, cy = mirror( j0 + b, Hh ) + 0.5 - 0.5 * Hh;
							backThrough( forward, H, cx, cy );
							qx[ b ][ a ] = cx;
							qy[ b ][ a ] = cy;
							inside       = inside && std::fabs( cx ) < 0.5 * W - 1.0 && std::fabs( cy ) < 0.5 * Hh - 1.0;
						}
					if( !inside )
						continue;
					const double px = ( qx[ 0 ][ 0 ] * ( 1 - fx ) + qx[ 0 ][ 1 ] * fx ) * ( 1 - fy ) + ( qx[ 1 ][ 0 ] * ( 1 - fx ) + qx[ 1 ][ 1 ] * fx ) * fy;
					const double py = ( qy[ 0 ][ 0 ] * ( 1 - fx ) + qy[ 0 ][ 1 ] * fx ) * ( 1 - fy ) + ( qy[ 1 ][ 0 ] * ( 1 - fx ) + qy[ 1 ][ 1 ] * fx ) * fy;
					//The trace to P' rounds (tol), and the map's own gradient (at
					//most the corners' spread per px) carries that into the
					//reading; rounding moves f by ~2^-24 x width, times the spread.
					const double spread = std::max( std::hypot( qx[ 0 ][ 0 ] - qx[ 1 ][ 1 ], qy[ 0 ][ 0 ] - qy[ 1 ][ 1 ] ),
					                                std::hypot( qx[ 0 ][ 1 ] - qx[ 1 ][ 0 ], qy[ 0 ][ 1 ] - qy[ 1 ][ 0 ] ) );
					const double t      = tol * ( 1.0 + spread ) + 4.0 * kEps32 * W * spread;
					const double e      = std::hypot( m.qx[ at ] - px, m.qy[ at ] - py );
					worstPred           = std::max( worstPred, e / t );
					over += e > t;
					const double blur = std::hypot( m.qx[ at ] - m.px( i ), m.qy[ at ] - m.py( j ) );
					blurs.push_back( blur );
					worstBlur = std::max( worstBlur, blur );
					sumBlur += blur;
					++measured;
				}
			std::sort( blurs.begin(), blurs.end() );
			//Bilinear across the cusp: the four corners span |Nx| + |Ny| px of
			//d, so the cusp's slope jump 2 alpha/lambda costs at most a quarter
			//of that each way; plus max|g''| (2 alpha/lambda^2) times span^2 / 8.
			const double span  = 0.6 + 0.8;
			const double bound = span * A / ( 2.0 * L ) + span * span * ( 2.0 * A / ( L * L ) ) / 8.0;
			const double p99   = blurs.empty() ? 0.0 : blurs[ blurs.size() * 99 / 100 ];
			size_t halfPx      = 0;
			for( double b : blurs )
				halfPx += b > 0.5;
			Check( over == 0 && measured > 0 && bakes >= 1 && worstBlur <= bound,
			       fmt( "%dx%d, %s: %d of %d pixels off the bilinear prediction (worst %.2f of tolerance); the bake's residue: worst %.2f px "
			            "(bound %.2f), mean %.3f px, 99th percentile %.3f px, %.2f%% of pixels over 0.5 px; %d pixels read the map beyond the window "
			            "(its mirror), not judged",
			            raster.w, raster.h, c.what, over, measured, worstPred, worstBlur, bound, measured ? sumBlur / measured : 0.0, p99,
			            measured ? 100.0 * halfPx / measured : 0.0, beyond ) );
		}
	return Verdict();
}
