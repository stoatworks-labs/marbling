//===========================================================================
// Time and the host: --rate, --clock, --resize, --paper, --alpha, --triggers.
//===========================================================================

/// Gelgit, eight big drops a trigger: two triggers cover the window, the
/// strokes follow; 20 ops by 3 s, so nothing is baked before then.
void rateSettings( Rig& rig )
{
	rig.Set( PT_PATTERN, static_cast< float >( Pattern::Gelgit ) );
	rig.Set( PT_DROP_RATE, ParamFromDropRate( 8 ) );
	rig.Set( PT_DROP_SIZE, ParamFromDropRadius( 0.2 ) );
	rig.Set( PT_SPEED, ParamFromStrokeSeconds( 0.6 ) );
	rig.Set( PT_DEPTH, ParamFromDepth( 0.15 ) );
	rig.Set( PT_SPACING, ParamFromSpacing( 0.03 ) );
	rig.Set( PT_AUTO_PAPER, 0.0f );
	rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Beat ) );
}

/// Pixels where two uv-card renders disagree by more than `px` (or one is
/// ink where the other is not), and the pixels on an edge in `a` (a kind or
/// colour change to a 4-neighbour): what a boundary moved by rounding can flip.
void compareMaps( const Mapped& a, const Mapped& b, double px, int& differ, int& edges, double& worst )
{
	differ = edges = 0;
	worst          = 0.0;
	for( int j = 0; j < a.h; ++j )
		for( int i = 0; i < a.w; ++i )
		{
			const size_t k = a.at( i, j );
			const bool same = a.label[ k ] == b.label[ k ] && ( a.label[ k ] != 0 || std::hypot( a.qx[ k ] - b.qx[ k ], a.qy[ k ] - b.qy[ k ] ) <= px );
			if( a.label[ k ] == 0 && b.label[ k ] == 0 )
				worst = std::max( worst, std::hypot( a.qx[ k ] - b.qx[ k ], a.qy[ k ] - b.qy[ k ] ) );
			differ += !same;
			if( i + 1 < a.w && j + 1 < a.h )
			{
				const size_t r = a.at( i + 1, j ), u = a.at( i, j + 1 );
				edges += a.label[ k ] != a.label[ r ] || a.label[ k ] != a.label[ u ] || std::hypot( a.qx[ k ] - a.qx[ r ], a.qy[ k ] - a.qy[ r ] ) > 2.0
				         || std::hypot( a.qx[ k ] - a.qx[ u ], a.qy[ k ] - a.qy[ u ] ) > 2.0;
			}
		}
}

//===========================================================================
// --rate: the same music at 24, 60 and 144 fps makes the same marbling.
//===========================================================================
int runRate( const Perturb& perturb )
{
	std::printf( "\n=== rate: the host's beat at 100 BPM and a press at 1.0 s, at 24, 60 and 144 fps: the same triggers at the same times, and the "
	             "same picture at 2.75 s (a time all three rates have a frame at)\n" );
	const Raster raster = kRasters.back();
	const double fpsList[] = { 60.0, 24.0, 144.0 };
	std::vector< std::vector< TriggerRecord > > logs;
	std::vector< Mapped > maps;
	for( double fps : fpsList )
	{
		const Floats uv = uvCard( raster.w, raster.h );
		Rig rig;
		rig.fps = fps;
		if( !startRig( rig, raster.w, raster.h, perturb, &uv ) )
			return 1;
		rateSettings( rig );
		rig.beatBpm     = 100.0;
		const int press = static_cast< int >( std::lround( fps * 1.0 ) );
		rig.beforeFrame = [ &rig, press ]( int f ) { rig.Set( PT_TRIGGER, f == press ? 1.0f : 0.0f ); };
		if( !rig.Render( static_cast< int >( std::lround( fps * 2.75 ) ) + 1 ) )
			return 1;
		logs.push_back( rig.plugin.TriggerLog() );
		maps.push_back( readMap( rig ) );
	}
	for( size_t r = 1; r < logs.size(); ++r )
	{
		int64_t worstUs = 0;
		bool sameCount  = logs[ r ].size() == logs[ 0 ].size();
		for( size_t k = 0; sameCount && k < logs[ 0 ].size(); ++k )
			worstUs = std::max< int64_t >( worstUs, std::llabs( logs[ r ][ k ].us - logs[ 0 ][ k ].us ) );
		std::string times;
		for( const TriggerRecord& t : logs[ r ] )
			times += fmt( " %.6f", t.us * 1e-6 );
		//The beat's phase reaches the plugin as a float: 2^-24 of a bar is
		//0.14 us at 100 BPM; the interpolation rounds to a microsecond.
		Check( sameCount && worstUs <= 2 && logs[ 0 ].size() == 6,
		       fmt( "%g fps against 60: %zu triggers against %zu, worst time apart %lld us (tolerance 2 us: the float phase and the us "
		            "rounding); at%s s",
		            fpsList[ r ], logs[ r ].size(), logs[ 0 ].size(), static_cast< long long >( worstUs ), times.c_str() ) );
		int differ = 0, edges = 0;
		double worst = 0.0;
		compareMaps( maps[ 0 ], maps[ r ], 0.01, differ, edges, worst );
		//Triggers 2 us apart move a stroke alpha x 2 us / 0.6 s, 4e-4 px at
		//108 px: an edge flips a pixel only where it passes that close to a
		//centre, far fewer than 1% of the edge pixels.
		Check( worst <= 0.01 || differ <= edges / 100 + 3,
		       fmt( "%g fps against 60, %dx%d at 2.75 s: %d pixels apart by more than 0.01 px or in kind (allowed 1%% of the %d edge pixels + 3); "
		            "worst map difference elsewhere %.2e px",
		            fpsList[ r ], raster.w, raster.h, differ, edges, worst ) );
	}
	return Verdict();
}

//===========================================================================
// --clock: Resolume's clock at 499,000,000 ms makes the same picture, bit
// for bit, as a clock in seconds from zero.
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: SetTime in milliseconds from 499,000,000 against seconds from 0: the same triggers and the same picture, bit for bit, "
	             "2.75 s in\n" );
	const Raster raster = kRasters.back();
	std::vector< Floats > outs;
	std::vector< std::vector< TriggerRecord > > logs;
	for( bool ms : { false, true } )
	{
		const Floats uv = uvCard( raster.w, raster.h );
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb, &uv ) )
			return 1;
		rateSettings( rig );
		rig.beatBpm      = 100.0;
		rig.msClock      = ms;
		rig.timeOffsetMs = ms ? 4.99e8 : 0.0;
		rig.plugin.SetClockScaleForTest( ms ? 0.001 : 1.0 );
		if( !rig.Render( 166 ) )
			return 1;
		outs.push_back( rig.Output() );
		logs.push_back( rig.plugin.TriggerLog() );
	}
	size_t differ = 0;
	for( size_t k = 0; k < outs[ 0 ].size(); ++k )
		differ += outs[ 0 ][ k ] != outs[ 1 ][ k ];
	bool sameLog = logs[ 0 ].size() == logs[ 1 ].size() && !logs[ 0 ].empty();
	for( size_t k = 0; sameLog && k < logs[ 0 ].size(); ++k )
		sameLog = logs[ 0 ][ k ].us == logs[ 1 ][ k ].us;
	Check( differ == 0 && sameLog,
	       fmt( "%dx%d: %zu of %zu floats differ; %zu triggers, at the same microseconds: %s", raster.w, raster.h, differ, outs[ 0 ].size(),
	            logs[ 1 ].size(), sameLog ? "yes" : "NO" ) );
	return Verdict();
}

//===========================================================================
// --resize: the host's raster changes under a finished marbling; the map is
// resampled by plane coordinates, predicted per pixel from the old picture.
//===========================================================================
/// Gelgit through its real triggers, each baked once it has finished:
/// drops on label cards, the map holding everything at the end.
bool marbleAndBake( Rig& rig, const Floats& uv )
{
	rig.plugin.SetBakeEveryTriggerForTest( true );
	rig.Set( PT_PATTERN, static_cast< float >( Pattern::Gelgit ) );
	rig.Set( PT_DROP_RATE, ParamFromDropRate( 4 ) );
	rig.Set( PT_DROP_SIZE, ParamFromDropRadius( 0.12 ) );
	rig.Set( PT_SPACING, ParamFromSpacing( 0.08 ) );
	rig.Set( PT_DEPTH, ParamFromDepth( 0.1 ) );
	rig.Set( PT_SPEED, ParamFromStrokeSeconds( 0.2 ) );
	rig.Set( PT_AUTO_PAPER, 0.0f );
	int label = 0;
	while( rig.plugin.BathForTest().CurrentStage() != Bath::Stage::Done && label < 30 )
	{
		++label;
		const bool drops = rig.plugin.BathForTest().CurrentStage() == Bath::Stage::Drops;
		rig.Upload( drops ? labelCard( rig.width, rig.height, label ) : uv );
		if( !rig.Press( PT_TRIGGER ) )
			return false;
		rig.Upload( uv );
		if( !rig.Render( 16 ) )
			return false;
	}
	return rig.plugin.BathForTest().Ops().empty() && !rig.plugin.BathForTest().MapIsIdentity();
}

int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: a baked marbling, then the host's raster changes: every new pixel is the old map read bilinear (nearest beside ink) "
	             "at the same plane point, its positions rescaled, predicted per pixel from the old picture\n" );
	const Raster from = kRasters.front();
	const Raster targets[] = { { std::max( 16, from.w / 2 ), std::max( 16, from.h / 2 ) }, { from.h, from.h } };
	for( const Raster& to : targets )
	{
		const Floats uv = uvCard( from.w, from.h );
		Rig rig;
		if( !startRig( rig, from.w, from.h, perturb, &uv ) )
			return 1;
		if( !marbleAndBake( rig, uv ) || !rig.Render( 1 ) )
		{
			Check( false, "the marbling did not finish baked" );
			continue;
		}
		const Mapped old  = readMap( rig );
		const Floats uvTo = uvCard( to.w, to.h );
		if( !rig.Resize( to.w, to.h, &uvTo ) || !rig.Render( 1 ) )
			return 1;
		const Mapped now = readMap( rig );
		const double rescale = static_cast< double >( to.h ) / from.h;
		auto mirror          = []( int i, int n ) {
            const int p = 2 * n;
            int k       = i % p;
            if( k < 0 )
                k += p;
            return k < n ? k : p - 1 - k;
		};
		int measured = 0, wrong = 0, inkPixels = 0, moved = 0;
		double worst = 0.0;
		for( int j = 0; j < now.h; ++j )
			for( int i = 0; i < now.w; ++i )
			{
				const double ox = now.px( i ) / rescale, oy = now.py( j ) / rescale;
				const double tx = ox + 0.5 * old.w - 0.5, ty = oy + 0.5 * old.h - 0.5;
				const int i0 = static_cast< int >( std::floor( tx ) ), j0 = static_cast< int >( std::floor( ty ) );
				const double fx = tx - i0, fy = ty - j0;
				size_t c[ 2 ][ 2 ];
				bool live = true, known = true;
				for( int b = 0; b < 2; ++b )
					for( int a = 0; a < 2; ++a )
					{
						c[ b ][ a ] = old.at( mirror( i0 + a, old.w ), mirror( j0 + b, old.h ) );
						live        = live && old.label[ c[ b ][ a ] ] == 0;
						known       = known && ( old.label[ c[ b ][ a ] ] != 0 || old.valid[ c[ b ][ a ] ] );
					}
				const size_t k = now.at( i, j );
				if( live )
				{
					if( !known || !now.valid[ k ] )
						continue;
					auto lerp = [ & ]( const std::vector< double >& q ) {
						return ( q[ c[ 0 ][ 0 ] ] * ( 1 - fx ) + q[ c[ 0 ][ 1 ] ] * fx ) * ( 1 - fy ) + ( q[ c[ 1 ][ 0 ] ] * ( 1 - fx ) + q[ c[ 1 ][ 1 ] ] * fx ) * fy;
					};
					const double px = lerp( old.qx ) * rescale, py = lerp( old.qy ) * rescale;
					const double spread = std::max( std::hypot( old.qx[ c[ 0 ][ 0 ] ] - old.qx[ c[ 1 ][ 1 ] ], old.qy[ c[ 0 ][ 0 ] ] - old.qy[ c[ 1 ][ 1 ] ] ),
					                                std::hypot( old.qx[ c[ 0 ][ 1 ] ] - old.qx[ c[ 1 ][ 0 ] ], old.qy[ c[ 0 ][ 1 ] ] - old.qy[ c[ 1 ][ 0 ] ] ) );
					//The old positions read to ~1e-4 px, the new to the same,
					//and f rounded at 2^-24 of the raster times the corners' spread.
					const double tol = 2e-3 + 4.0 * kEps32 * old.w * spread;
					const double e   = std::hypot( now.qx[ k ] - px, now.qy[ k ] - py );
					worst            = std::max( worst, e / tol );
					wrong += e > tol;
					moved += std::hypot( now.qx[ k ] - now.px( i ), now.qy[ k ] - now.py( j ) ) > 1.0;
					++measured;
				}
				else
				{
					const size_t n = old.at( mirror( static_cast< int >( std::floor( tx + 0.5 ) ), old.w ), mirror( static_cast< int >( std::floor( ty + 0.5 ) ), old.h ) );
					if( old.label[ n ] == 0 && ( !old.valid[ n ] || !now.valid[ k ] ) )
						continue;
					++measured;
					if( old.label[ n ] != 0 )
					{
						++inkPixels;
						wrong += now.label[ k ] != old.label[ n ];
					}
					else
					{
						const double e = std::hypot( now.qx[ k ] - old.qx[ n ] * rescale, now.qy[ k ] - old.qy[ n ] * rescale );
						worst          = std::max( worst, e / 2e-3 );
						wrong += now.label[ k ] != 0 || e > 2e-3;
					}
				}
			}
		Check( wrong == 0 && measured > now.w * now.h / 2 && moved > measured / 10 && inkPixels > 0,
		       fmt( "%dx%d to %dx%d: %d of %d pixels off the prediction (worst %.2f of its tolerance); %d ink pixels kept their ink; %d pixels "
		            "displaced over 1 px (the marbling survived)",
		            from.w, from.h, to.w, to.h, wrong, measured, worst, inkPixels, moved ) );
	}
	return Verdict();
}

//===========================================================================
// --paper: Paper lays a clean sheet, exactly; the next trigger drops.
//===========================================================================
int runPaper( const Perturb& perturb )
{
	std::printf( "\n=== paper: after a finished marbling, Paper (and Auto Paper's trigger) gives back the clip bit for bit; the next trigger starts "
	             "the drops again\n" );
	for( const Raster& raster : kRasters )
		for( bool autoPaper : { false, true } )
		{
			const Floats uv = uvCard( raster.w, raster.h );
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb, &uv ) )
				return 1;
			if( !marbleAndBake( rig, uv ) )
			{
				Check( false, "the marbling did not finish" );
				continue;
			}
			rig.plugin.SetBakeEveryTriggerForTest( false );
			int movedBefore = 0;
			{
				const Mapped m = readMap( rig );
				for( size_t k = 0; k < m.qx.size(); ++k )
					movedBefore += m.label[ k ] != 0 || std::fabs( m.qx[ k ] - m.px( static_cast< int >( k % m.w ) ) ) > 1.0;
			}
			rig.Set( PT_AUTO_PAPER, autoPaper ? 1.0f : 0.0f );
			if( !rig.Press( autoPaper ? PT_TRIGGER : PT_PAPER ) )
				return 1;
			const Floats o = rig.Output();
			size_t differ  = 0;
			for( size_t k = 0; k < o.size(); ++k )
				differ += o[ k ] != uv[ k ];
			const bool logged = !rig.plugin.TriggerLog().empty() && rig.plugin.TriggerLog().back().paper;
			if( !rig.Press( PT_TRIGGER ) || !rig.Render( 30 ) )
				return 1;
			const auto& ops = rig.plugin.BathForTest().Ops();
			int drops       = 0;
			for( const Op& op : ops )
				drops += op.type == OpType::Drop;
			Check( differ == 0 && logged && movedBefore > 1000 && drops == 4 && static_cast< int >( ops.size() ) == 4,
			       fmt( "%dx%d, %s: %d pixels marbled before; %zu floats differ from the clip after; logged as a sheet: %s; the next trigger "
			            "made %d ops, %d of them drops (Drop Rate 4)",
			            raster.w, raster.h, autoPaper ? "Auto Paper's trigger" : "Paper", movedBefore, differ, logged ? "yes" : "NO",
			            static_cast< int >( ops.size() ), drops ) );
		}
	return Verdict();
}

//===========================================================================
// --alpha: Mix 0 is the clip bit for bit; what is under alpha 0 never
// shows; an opaque clip stays opaque; suminagashi's clear drops are clear.
//===========================================================================
Floats alphaCard( int w, int h, bool whiteUnder )
{
	Floats c = lookCard( w, h );
	for( int j = 0; j < h; ++j )
		for( int i = 0; i < w; ++i )
		{
			float* p     = &c[ ( static_cast< size_t >( j ) * w + i ) * 4 ];
			const double u = ( i + 0.5 ) / w, v = ( j + 0.5 ) / h;
			//A transparent band down the middle, a half-transparent disc.
			const bool clear = u > 0.35 && u < 0.6;
			const bool half  = std::hypot( u - 0.2, v - 0.7 ) < 0.12;
			p[ 3 ]           = clear ? 0.0f : half ? 0.5f : 1.0f;
			if( clear )
				p[ 0 ] = p[ 1 ] = p[ 2 ] = whiteUnder ? 1.0f : 0.0f;
		}
	return c;
}

int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: output alpha is ink density (SPEC-marbling.md): Mix 0 hands the clip through bit for bit; white or black under alpha 0 "
	             "makes no difference at any Mix; an opaque clip stays opaque; a clear drop is transparent\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		//(a) Mix 0.
		{
			const Floats card = alphaCard( w, h, true );
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card ) )
				return 1;
			rig.Set( PT_MIX, 0.0f );
			for( int k = 0; k < 6; ++k )
				if( !rig.Press( PT_TRIGGER ) || !rig.Render( 20 ) )
					return 1;
			const Floats o = rig.Output();
			size_t differ  = 0;
			for( size_t k = 0; k < o.size(); ++k )
				differ += o[ k ] != card[ k ];
			Check( differ == 0, fmt( "%dx%d, Mix 0 after six triggers: %zu of %zu floats differ from the clip", w, h, differ, o.size() ) );
		}
		//(b) White under transparent against black, at Mix 1 and 0.5.
		for( float mix : { 1.0f, 0.5f } )
		{
			std::vector< Floats > outs;
			for( bool white : { true, false } )
			{
				const Floats card = alphaCard( w, h, white );
				Rig rig;
				if( !startRig( rig, w, h, perturb, &card ) )
					return 1;
				rig.Set( PT_MIX, mix );
				rig.Set( PT_DROP_SIZE, ParamFromDropRadius( 0.15 ) );
				for( int k = 0; k < 10; ++k )
					if( !rig.Press( PT_TRIGGER ) || !rig.Render( 40 ) )
						return 1;
				outs.push_back( rig.Output() );
			}
			size_t differ = 0;
			for( size_t k = 0; k < outs[ 0 ].size(); ++k )
				differ += outs[ 0 ][ k ] != outs[ 1 ][ k ];
			Check( differ == 0, fmt( "%dx%d, Mix %.1f, ten triggers of drops and strokes: white under alpha 0 against black, %zu floats differ", w, h,
			                         mix, differ ) );
		}
		//(c) Opaque stays opaque. (d) A clear drop is transparent.
		{
			const Floats card = lookCard( w, h );
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card ) )
				return 1;
			for( int k = 0; k < 10; ++k )
				if( !rig.Press( PT_TRIGGER ) || !rig.Render( 40 ) )
					return 1;
			const Floats o = rig.Output();
			double lowest  = 1.0;
			for( size_t k = 3; k < o.size(); k += 4 )
				lowest = std::min( lowest, static_cast< double >( o[ k ] ) );
			//Bilinear weights summing to one in float: 1 - 2^-23 at worst.
			Check( lowest >= 1.0 - 4.0 * kEps32 * 2.0, fmt( "%dx%d, an opaque clip, ten triggers: lowest output alpha 1 - %.1e", w, h, 1.0 - lowest ) );
		}
		{
			const Floats card = lookCard( w, h );
			Rig rig;
			if( !startRig( rig, w, h, perturb, &card ) )
				return 1;
			const double r = 0.1;
			rig.Set( PT_PATTERN, static_cast< float >( Pattern::Suminagashi ) );
			rig.Set( PT_DROP_RATE, ParamFromDropRate( 1 ) );
			rig.Set( PT_DROP_SIZE, ParamFromDropRadius( r ) );
			for( int k = 0; k < 2; ++k )
				if( !rig.Press( PT_TRIGGER ) || !rig.Render( 60 ) )
					return 1;
			const Floats o = rig.Output();
			int clearWrong = 0, inkWrong = 0, clearN = 0, inkN = 0;
			const double R = r * h;
			for( int j = 0; j < h; ++j )
				for( int i = 0; i < w; ++i )
				{
					const double rho = std::hypot( i + 0.5 - 0.5 * w, j + 0.5 - 0.5 * h );
					const float a    = o[ ( static_cast< size_t >( j ) * w + i ) * 4 + 3 ];
					if( rho < R - 1.0 )
					{
						++clearN;
						clearWrong += a != 0.0f;
					}
					else if( rho > R + 1.0 && rho < R * std::sqrt( 2.0 ) - 1.0 )
					{
						++inkN;
						inkWrong += a < 1.0f - 1e-6f;
					}
				}
			Check( clearWrong == 0 && inkWrong == 0 && clearN > 0 && inkN > 0,
			       fmt( "%dx%d, Suminagashi, an ink drop then a clear one at the centre: %d of %d pixels inside the clear drop not transparent, %d of "
			            "%d in the ink's ring (r .. r sqrt 2) not opaque",
			            w, h, clearWrong, clearN, inkWrong, inkN ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --triggers: presses, the host's beat (and the plugin's own when the host's
// never moves), and onsets, each where it belongs on the plugin's clock.
//===========================================================================
int runTriggers( const Perturb& perturb )
{
	std::printf( "\n=== triggers: a press is one trigger at its frame; the beat's quarter-bar crossings land inside the frame at any rate; a host "
	             "whose bar phase never moves gets the plugin's own beat; onsets land where earshot heard them\n" );
	const Raster raster = kRasters.back();
	auto startQuiet     = [ & ]( Rig& rig, double fps ) {
        rig.fps = fps;
        if( !startRig( rig, raster.w, raster.h, perturb ) )
            return false;
        rig.Set( PT_PATTERN, static_cast< float >( Pattern::Stone ) );
        rig.Set( PT_DROP_SIZE, ParamFromDropRadius( 0.02 ) );
        return true;
	};
	//(a) Presses: held across frames, still one.
	{
		Rig rig;
		if( !startQuiet( rig, 60.0 ) )
			return 1;
		rig.beforeFrame = [ &rig ]( int f ) {
			const bool down = ( f >= 10 && f < 14 ) || f == 20 || f == 22;
			rig.Set( PT_TRIGGER, down ? 1.0f : 0.0f );
		};
		if( !rig.Render( 40 ) )
			return 1;
		const auto& log = rig.plugin.TriggerLog();
		const int64_t expect[] = { std::llround( 10 / 60.0 * 1e6 ), std::llround( 20 / 60.0 * 1e6 ), std::llround( 22 / 60.0 * 1e6 ) };
		bool ok = log.size() == 3;
		for( size_t k = 0; ok && k < 3; ++k )
			ok = log[ k ].us == expect[ k ];
		Check( ok, fmt( "presses at frames 10 (held to 13), 20 and 22 at 60 fps: %zu triggers, at the frames' microseconds: %s", log.size(), ok ? "yes" : "NO" ) );
	}
	//(b) The host's beat, 120 BPM, at 60 and 24 fps; (c) a host that never
	//moves its bar phase.
	for( int variant = 0; variant < 3; ++variant )
	{
		const double fps = variant == 1 ? 24.0 : 60.0;
		Rig rig;
		if( !startQuiet( rig, fps ) )
			return 1;
		rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Beat ) );
		if( variant < 2 )
			rig.beatBpm = 120.0;
		else
			rig.beforeFrame = [ &rig ]( int ) { rig.plugin.SetBeatInfo( 120.0f, 0.0f ); };
		if( !rig.Render( static_cast< int >( std::lround( 4.2 * fps ) ) ) )
			return 1;
		const auto& log = rig.plugin.TriggerLog();
		int64_t worst   = 0;
		for( size_t k = 0; k < log.size(); ++k )
			worst = std::max< int64_t >( worst, std::llabs( log[ k ].us - static_cast< int64_t >( k + 1 ) * 500000 ) );
		Check( log.size() == 8 && worst <= 2,
		       fmt( "%s at %g fps, 4.2 s: %zu triggers (8 expected, every 0.5 s), worst %lld us from k x 0.5 s (tolerance 2 us)",
		            variant < 2 ? "the host's beat, 120 BPM," : "a bar phase stuck at 0, 120 BPM: the plugin's own beat", fps, log.size(),
		            static_cast< long long >( worst ) ) );
	}
	//(d) Onsets: the harness's kicks (every 0.5 s) and hats (between), fed
	//through earshot's feed() as a device delivers them.
	for( double fps : { 60.0, 24.0 } )
	{
		Rig rig;
		if( !startQuiet( rig, fps ) )
			return 1;
		rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Onsets ) );
		rig.audio = musicAt;
		if( !rig.Render( static_cast< int >( std::lround( 4.1 * fps ) ) ) )
			return 1;
		//earshot stamps an onset at the end of the first 1024-sample window
		//(21.3 ms) whose flux crosses, and analyses whole 256-sample hops
		//(5.3 ms): a trigger lands 0 .. 26.7 ms after its event.
		const double late = ( 1024.0 + 256.0 ) / kRate + 1e-6;
		int unmatched = 0, missed = 0, doubled = 0, n = 0;
		double worst = 0.0;
		std::vector< int > hits( 16, 0 );
		for( const TriggerRecord& t : rig.plugin.TriggerLog() )
		{
			const double s = t.us * 1e-6;
			if( s < 1.0 )
				continue;
			++n;
			const int event = static_cast< int >( std::floor( s / 0.25 ) );
			const double lag = s - event * 0.25;
			if( lag < -1e-6 || lag > late )
				++unmatched;
			else if( event < 16 )
				++hits[ static_cast< size_t >( event ) ];
			worst = std::max( worst, lag );
		}
		for( int e = 4; e < 16; ++e )
		{
			missed += hits[ static_cast< size_t >( e ) ] == 0;
			doubled += hits[ static_cast< size_t >( e ) ] > 1;
		}
		Check( unmatched == 0 && missed == 0 && doubled == 0,
		       fmt( "onsets at %g fps, 1..4 s: %d triggers; %d outside 0..%.1f ms of a kick or hat, %d of 12 events missed, %d doubled; latest %.1f "
		            "ms after its event",
		            fps, n, unmatched, late * 1e3, missed, doubled, worst * 1e3 ) );
	}
	return Verdict();
}
