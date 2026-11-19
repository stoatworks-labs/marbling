//===========================================================================
// Time and the host: --rate, --clock, --resize, --paper, --alpha, --triggers, --hostfft.
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
		Check( sameCount && worstUs <= 2 && logs[ 0 ].size() == 5,
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
	//Auto Paper waits for the last stroke: Stone, eight drops of the largest
	//size a trigger, so the drop stage ends on the second trigger (8 pi
	//0.25^2 = 1.57 frame heights squared, short of the window's 16/9); a
	//trigger while those drops still grow does nothing, and the first one
	//after they finish lays the sheet.
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_PATTERN, static_cast< float >( Pattern::Stone ) );
		rig.Set( PT_DROP_RATE, ParamFromDropRate( 8 ) );
		rig.Set( PT_DROP_SIZE, ParamFromDropRadius( 0.25 ) );
		rig.Set( PT_SPEED, ParamFromStrokeSeconds( 2.0 ) );//drops grow over 1 s
		if( !rig.Render( 5 ) || !rig.Press( PT_TRIGGER ) || !rig.Render( 2 ) || !rig.Press( PT_TRIGGER ) )
			return 1;
		const bool done   = rig.plugin.BathForTest().CurrentStage() == Bath::Stage::Done;
		const size_t made = rig.plugin.BathForTest().Ops().size();
		if( !rig.Render( 20 ) || !rig.Press( PT_TRIGGER ) )
			return 1;
		const bool heldPaper = rig.plugin.BathForTest().Papers() == 0 && rig.plugin.BathForTest().Ops().size() == made;
		if( !rig.Render( 60 ) || !rig.Press( PT_TRIGGER ) )
			return 1;
		const bool laid = rig.plugin.BathForTest().Papers() == 1 && rig.plugin.BathForTest().Ops().empty();
		Check( done && made == 16 && heldPaper && laid,
		       fmt( "%dx%d, Auto Paper: the drop stage done on the second trigger (%s, %zu drops, each growing for 1 s); a trigger 21 frames "
		            "(0.35 s) later lays nothing (%s); one 82 frames (1.37 s) later, after they finished, lays the sheet (%s)",
		            raster.w, raster.h, done ? "yes" : "NO", made, heldPaper ? "yes" : "NO", laid ? "yes" : "NO" ) );
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
	//(b) The host's beat, 100 BPM (a beat every 0.6 s, off the 24 fps
	//grid, so a crossing placed at the frame shows), at 60 and 24 fps;
	//(c) a host that never moves its bar phase.
	for( int variant = 0; variant < 3; ++variant )
	{
		const double fps = variant == 1 ? 24.0 : 60.0;
		Rig rig;
		if( !startQuiet( rig, fps ) )
			return 1;
		rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Beat ) );
		if( variant < 2 )
			rig.beatBpm = 100.0;
		else
			rig.beforeFrame = [ &rig ]( int ) { rig.plugin.SetBeatInfo( 100.0f, 0.0f ); };
		if( !rig.Render( static_cast< int >( std::lround( 4.0 * fps ) ) ) )
			return 1;
		const auto& log = rig.plugin.TriggerLog();
		int64_t worst   = 0;
		for( size_t k = 0; k < log.size(); ++k )
			worst = std::max< int64_t >( worst, std::llabs( log[ k ].us - static_cast< int64_t >( k + 1 ) * 600000 ) );
		Check( log.size() == 6 && worst <= 2,
		       fmt( "%s at %g fps, 4 s: %zu triggers (6 expected, every 0.6 s), worst %lld us from k x 0.6 s (tolerance 2 us)",
		            variant < 2 ? "the host's beat, 100 BPM," : "a bar phase stuck at 0, 100 BPM: the plugin's own beat", fps, log.size(),
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

//===========================================================================
// --hostfft: Onsets on Resolume's own FFT, earshot's host path alone. A drum
// loop becomes the 64 bins Arena 7.29.0 was measured to send (earshot's
// AGENTS.md, "Resolume's 64 bins, measured") and is handed to the plugin as
// its FF_USAGE_FFT buffer, element by element, as a host does; nothing goes
// through feed(). Under earshot 8124191 (each bin whole to the band holding
// its centre) the lowest band, 40-160 Hz, got no bin and never fired: a kick
// triggered through 160-640 Hz alone.
//===========================================================================

/// Arena's FFT buffer as measured: every 2048 samples of the engine's rate, a
/// 2048-point Hann-windowed DFT of the newest 2048 samples (the mono sum
/// (L + R) / 2: the loop is the same on both sides), and bin k the sum of
/// |X_i| / 2N over the 16 fine bins 16k .. 16k + 15. That is linear, fs / 128
/// a bin (375 Hz at 48 kHz) to fs / 2, a magnitude, a sine of amplitude A
/// reading A / 4 in its bin and 0.75 of that split 1 : 3 on a boundary, as
/// measured. Held between updates, as Arena holds it. (snowglobe's sgtest.)
struct HostSpectrum
{
	static constexpr int kN = 2048;
	int rate;
	std::function< double( double ) > signal;
	std::vector< float > bins = std::vector< float >( 64, 0.0f );
	int64_t updated           = -1;

	HostSpectrum( int r, std::function< double( double ) > s ) : rate( r ), signal( std::move( s ) ) {}

	const std::vector< float >& at( double t )
	{
		const int64_t u = static_cast< int64_t >( std::floor( t * rate / kN + 1e-9 ) );
		if( u == updated )
			return bins;
		updated = u;
		std::vector< std::complex< double > > x( kN );
		for( int n = 0; n < kN; ++n )
		{
			const int64_t sample = u * kN - kN + n;
			const double hann    = 0.5 - 0.5 * std::cos( 2.0 * kPi * n / kN );
			x[ static_cast< size_t >( n ) ] = hann * signal( static_cast< double >( sample ) / rate );
		}
		//Radix-2, in place.
		for( int i = 1, j = 0; i < kN; ++i )
		{
			int bit = kN >> 1;
			for( ; j & bit; bit >>= 1 )
				j ^= bit;
			j ^= bit;
			if( i < j )
				std::swap( x[ static_cast< size_t >( i ) ], x[ static_cast< size_t >( j ) ] );
		}
		for( int len = 2; len <= kN; len <<= 1 )
		{
			const std::complex< double > w = std::polar( 1.0, -2.0 * kPi / len );
			for( int i = 0; i < kN; i += len )
			{
				std::complex< double > wk( 1.0, 0.0 );
				for( int k = 0; k < len / 2; ++k )
				{
					const auto a = x[ static_cast< size_t >( i + k ) ], b = x[ static_cast< size_t >( i + k + len / 2 ) ] * wk;
					x[ static_cast< size_t >( i + k ) ]           = a + b;
					x[ static_cast< size_t >( i + k + len / 2 ) ] = a - b;
					wk *= w;
				}
			}
		}
		for( int k = 0; k < 64; ++k )
		{
			double sum = 0.0;
			for( int i = 16 * k; i < 16 * k + 16; ++i )
				sum += std::abs( x[ static_cast< size_t >( i ) ] );
			bins[ static_cast< size_t >( k ) ] = static_cast< float >( sum / ( 2.0 * kN ) );
		}
		return bins;
	}
};

/// 128 BPM (Arena's Beat 001), so the hits fall at every phase of the
/// 2048-sample updates. The parts: the harness's kick on every beat; a clap on
/// two and four and hats on the eighths between, as noise twice differenced
/// (-52 dB at 375 Hz), so they put nothing in bin 0; a 49 Hz bass held
/// throughout, in bin 0 with the kick.
constexpr double kLoopBeat = 60.0 / 128.0;
enum LoopPart : int
{
	kLoopKick = 1,
	kLoopTop  = 2,///< claps and hats
	kLoopBass = 4
};

double drumLoopAt( double t, int parts, int rate )
{
	if( t < 0.0 )
		return 0.0;
	//The sample's own index, rounded: truncating t * rate repeats and skips
	//samples, and a differenced noise that does is no longer high-passed.
	const uint32_t n  = static_cast< uint32_t >( std::llround( t * rate ) );
	const double beat = std::fmod( t, kLoopBeat );
	double s          = 0.0;
	if( parts & kLoopBass )
		s += 0.2 * std::sin( 2.0 * kPi * 49.0 * t );
	if( parts & kLoopKick )
		s += 0.6 * std::exp( -beat / 0.06 ) * std::sin( 2.0 * kPi * ( 55.0 + 60.0 * std::exp( -beat / 0.02 ) ) * beat );
	if( parts & kLoopTop )
	{
		auto hiss = [ n ]( uint32_t seed ) { return Hash01( n, seed ) - 2.0 * Hash01( n - 1u, seed ) + Hash01( n - 2u, seed ); };
		const double clap = std::fmod( t, 2.0 * kLoopBeat ) - kLoopBeat;
		if( clap >= 0.0 )
			s += 0.3 * std::exp( -clap / 0.03 ) * hiss( 11u );
		const double hat = std::fmod( t + kLoopBeat / 2.0, kLoopBeat );
		s += 0.2 * std::exp( -hat / 0.01 ) * hiss( 7u );
	}
	return s;
}

/// What the plugin did on Arena-made bins: each band's onsets and every
/// trigger, in seconds on the plugin's clock, and the rate earshot read at.
struct HostRun
{
	std::vector< double > band[ earshot::kMaxBands ];
	std::vector< double > triggers;
	int bandCount   = 0;
	double hostRate = 0.0;
};

/// The bins made at `rate`; the plugin told `lawRate` by SetSampleRate (-1:
/// `rate`, 0: nothing sent). 320x180, Stone with small drops (the picture is
/// not judged), Onsets on Resolume FFT.
bool runOnHostBins( const Perturb& perturb, int rate, const std::function< double( double ) >& signal, double seconds, HostRun& out, int lawRate = -1 )
{
	const Raster raster = kRasters.back();
	Rig rig;
	if( !startRig( rig, raster.w, raster.h, perturb ) )
		return false;
	if( lawRate < 0 )
		lawRate = rate;
	if( lawRate > 0 )
		rig.plugin.SetSampleRate( static_cast< unsigned int >( lawRate ) );
	rig.Set( PT_PATTERN, static_cast< float >( Pattern::Stone ) );
	rig.Set( PT_DROP_SIZE, ParamFromDropRadius( 0.02 ) );
	rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Onsets ) );
	rig.Set( PT_INPUT, 0.0f );//Resolume FFT
	HostSpectrum host( rate, signal );
	uint32_t seen[ earshot::kMaxBands ] = {};
	const int frames = static_cast< int >( std::lround( seconds * rig.fps ) );
	for( int f = 0; f < frames; ++f )
	{
		const double t                = rig.Seconds( rig.frame );
		const std::vector< float >& b = host.at( t );
		for( unsigned int j = 0; j < 64; ++j )
			rig.plugin.SetParamElementValue( PT_AUDIO, j, b[ j ] );
		if( !rig.Render( 1 ) )
			return false;
		const earshot::Frame& fr = rig.plugin.AudioForTest().Frame();
		out.bandCount            = std::min( fr.bandCount, earshot::kMaxBands );
		for( int k = 0; k < out.bandCount; ++k )
		{
			//The first frame primes: counts already standing are not onsets.
			if( f > 0 )
				for( uint32_t n = seen[ k ]; n < fr.bands[ k ].onsetCount; ++n )
					out.band[ k ].push_back( t );
			seen[ k ] = fr.bands[ k ].onsetCount;
		}
	}
	for( const TriggerRecord& r : rig.plugin.TriggerLog() )
		out.triggers.push_back( static_cast< double >( r.us ) * 1e-6 );
	out.hostRate = rig.plugin.AudioForTest().HostRateForTest();
	return true;
}

/// Events against hits: each event matched to the latest hit at or before it
/// within `late`. Hits before `from` or too near the end are not counted.
struct Matched
{
	int events = 0, hits = 0, missed = 0, doubled = 0, unmatched = 0;
	double worst = 0.0;
};

Matched matchHits( const std::vector< double >& events, const std::vector< double >& hits, double late, double from, double to )
{
	Matched m;
	std::vector< int > per( hits.size(), 0 );
	for( double t : events )
	{
		if( t < from )
			continue;
		++m.events;
		int best = -1;
		for( size_t h = 0; h < hits.size(); ++h )
			if( hits[ h ] <= t + 1e-9 && t - hits[ h ] <= late )
				best = static_cast< int >( h );
		if( best < 0 )
			++m.unmatched;
		else
		{
			++per[ static_cast< size_t >( best ) ];
			m.worst = std::max( m.worst, t - hits[ static_cast< size_t >( best ) ] );
		}
	}
	for( size_t h = 0; h < hits.size(); ++h )
	{
		if( hits[ h ] < from || hits[ h ] + late > to )
			continue;
		++m.hits;
		m.missed += per[ h ] == 0;
		m.doubled += per[ h ] > 1;
	}
	return m;
}

int runHostFft( const Perturb& perturb )
{
	std::printf( "\n=== hostfft: Onsets on Resolume's FFT alone (the bins as Arena 7.29.0 sends them, nothing fed), at 128 BPM: kicks alone fire "
	             "the lowest band (40-160 Hz) once each, in the frame 160-640 Hz fires, the two one trigger, every kick triggering, at 48 and at "
	             "44.1 kHz, within two of the host's updates and a frame; in a drum loop the lowest band fires on every kick and nothing else, "
	             "and every hit triggers; claps and hats alone trigger once each, and the lowest band never on its own; over a bass held in "
	             "bin 0 no kick is missed by the lowest band; earshot reads the bins at the host's rate\n" );
	const double kSeconds = 6.0, kFrom = 1.0;//the first second primes earshot's floors
	struct Run
	{
		int rate;
		int parts;
		const char* what;
	};
	const Run runs[] = {
		{ 48000, kLoopKick, "kicks alone" },
		{ 44100, kLoopKick, "kicks alone" },
		{ 48000, kLoopKick | kLoopTop, "the drum loop" },
		{ 48000, kLoopTop, "claps and hats alone" },
		{ 48000, kLoopKick | kLoopTop | kLoopBass, "the drum loop over a held 49 Hz bass" },
	};
	for( const Run& run : runs )
	{
		const int parts = run.parts, rate = run.rate;
		HostRun r;
		if( !runOnHostBins( perturb, run.rate, [ parts, rate ]( double t ) { return drumLoopAt( t, parts, rate ); }, kSeconds, r ) )
			return 1;
		//Seen by the second update after the hit at the latest (the first may
		//hold only its window's Hann-weighted tail), then read at the next frame.
		const double late = 2.0 * HostSpectrum::kN / run.rate + 1.0 / 60.0 + 1e-6;
		std::vector< double > kicks, hits;
		for( int k = 0; k * kLoopBeat < kSeconds; ++k )
		{
			if( parts & kLoopKick )
				kicks.push_back( k * kLoopBeat );
			if( ( parts & kLoopKick ) || ( ( parts & kLoopTop ) && k % 2 == 1 ) )
				hits.push_back( k * kLoopBeat );//a kick, or a clap alone
			if( parts & kLoopTop )
				hits.push_back( ( k + 0.5 ) * kLoopBeat );
		}
		std::string perBand;
		for( int b = 0; b < r.bandCount; ++b )
		{
			int n = 0;
			for( double t : r.band[ b ] )
				n += t >= kFrom;
			perBand += fmt( "%s%d", b == 0 ? "" : " / ", n );
		}
		//A lowest-band onset on a frame where no other band fired: a trigger
		//of its own. On a frame where one did, the merge makes them one.
		int alone = 0, withMid = 0, low = 0;
		for( double t : r.band[ 0 ] )
		{
			if( t < kFrom )
				continue;
			++low;
			bool other = false;
			for( int b = 1; b < r.bandCount; ++b )
				other = other || std::find( r.band[ b ].begin(), r.band[ b ].end(), t ) != r.band[ b ].end();
			alone += !other;
			withMid += std::find( r.band[ 1 ].begin(), r.band[ 1 ].end(), t ) != r.band[ 1 ].end();
		}
		int sameFrame = 0;
		for( size_t k = 1; k < r.triggers.size(); ++k )
			sameFrame += r.triggers[ k ] == r.triggers[ k - 1 ];
		Info( fmt( "%s, %d Hz, 1..6 s: onsets by band (40-160 / 160-640 / 640-2560 / 2560-8000 Hz) %s; %zu triggers in all", run.what, run.rate,
		           perBand.c_str(), r.triggers.size() ) );
		const Matched lo  = matchHits( r.band[ 0 ], kicks, late, kFrom, kSeconds );
		const Matched all = matchHits( r.triggers, hits, late, kFrom, kSeconds );
		const std::string loLine =
			fmt( "%s, %d Hz, 1..6 s: the lowest band fired %d times for %d kicks; %d missed, %d doubled, latest %.1f ms after its kick (limit %.1f); "
		         "%d not within that of a kick",
		         run.what, run.rate, lo.events, lo.hits, lo.missed, lo.doubled, lo.worst * 1e3, late * 1e3, lo.unmatched );
		const std::string allLine = fmt( "%s, %d Hz, 1..6 s: %d triggers for %d hits; %d missed, %d doubled, latest %.1f ms after its hit; %d not within "
		                                 "that of a hit; %d pairs in one frame",
		                                 run.what, run.rate, all.events, all.hits, all.missed, all.doubled, all.worst * 1e3, all.unmatched, sameFrame );
		if( parts == kLoopKick )
		{
			Check( lo.events > 0 && lo.missed == 0 && lo.doubled == 0 && lo.unmatched == 0, loLine );
			Check( withMid == low, fmt( "%s, %d Hz: %d of the lowest band's %d onsets on a frame 160-640 Hz fired too (want all: a kick is in bin 0, "
			                            "which both bands share)",
			                            run.what, run.rate, withMid, low ) );
			//The two bands bin 0 feeds fire in one frame and make one trigger. A
			//kick's attack can reach 640-2560 Hz one host update (43 ms) before
			//its body crosses in bin 0, and two updates seen three frames apart
			//are 50 ms apart, the merge window: a second trigger, counted.
			Check( all.events > 0 && all.missed == 0 && all.unmatched == 0 && sameFrame == 0,
			       allLine + " (want every kick, its two low bands one trigger; a second trigger an update later, from the attack reaching "
			                 "640-2560 Hz first, counted, not refused)" );
		}
		else if( !( parts & kLoopKick ) )
		{
			Check( all.events > 0 && all.missed == 0 && all.doubled == 0 && all.unmatched == 0 && sameFrame == 0, allLine );
			Check( alone == 0, fmt( "%s, %d Hz: the lowest band fired %d times, %d of them on a frame no other band fired (want none: it adds no "
			                        "trigger of its own)",
			                        run.what, run.rate, low, alone ) );
		}
		else if( parts & kLoopBass )
			Check( lo.events > 0 && lo.missed == 0, loLine + " (counted, not refused: a held bass in bin 0 beats against the kick's tail)" );
		else
		{
			Check( lo.events > 0 && lo.missed == 0 && lo.doubled == 0 && lo.unmatched == 0, loLine );
			//A hit's attack (a kick's, a clap's crack over a kick) can reach the
			//upper bands one host update before its low end crosses, 50 ms
			//apart once frame-quantised: a second trigger, counted, not refused.
			Check( all.events > 0 && all.missed == 0 && all.unmatched == 0 && sameFrame == 0,
			       allLine + " (doubles counted, not refused: a hit's attack can reach the upper bands one host update before its low end)" );
		}
		if( run.rate != 48000 || parts == kLoopKick )
			Check( std::fabs( r.hostRate - run.rate ) < 1e-9, fmt( "the host says %d Hz: earshot reads its bins at %.0f Hz", run.rate, r.hostRate ) );
	}
	//A rate no engine runs at is not one: earshot keeps its 48 kHz.
	{
		const Raster raster = kRasters.back();
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.plugin.SetSampleRate( 0u );
		rig.Set( PT_TRIGGER_ON, static_cast< float >( TriggerOn::Onsets ) );
		if( !rig.Render( 2 ) )
			return 1;
		const double hostRate = rig.plugin.AudioForTest().HostRateForTest();
		Check( std::fabs( hostRate - 48000.0 ) < 1e-9, fmt( "a host rate of 0: earshot keeps %.0f Hz (want 48000)", hostRate ) );
	}
	return Verdict();
}

//===========================================================================
// --hostwav --wav PATH: not a check, a measurement. The WAV, looped, made into
// Arena's bins at 48 kHz as above, played to the plugin for --frames frames
// at 60 fps under earshot 0.1.1 (bins shared, read at the 48 kHz the host
// sends) and as v0.1.0 read them (8124191: each bin whole to the band
// holding its centre, read as 64 bins to 22.05 kHz): the onsets by band, the
// triggers, and how many of the lowest band's onsets came on a frame where
// 160-640 Hz fired too.
//===========================================================================
int runHostWav( double seconds )
{
	if( g_wav.mono.empty() )
	{
		std::fprintf( stderr, "--hostwav needs --wav PATH\n" );
		return 2;
	}
	std::printf( "\n=== hostwav: a WAV (%d Hz, %.2f s, looped) made into Arena's 64 bins at 48 kHz, %.0f s at 60 fps, Onsets on Resolume FFT\n",
	             g_wav.rate, static_cast< double >( g_wav.mono.size() ) / g_wav.rate, seconds );
	for( int rule = 0; rule < 2; ++rule )
	{
		Perturb p;
		p.cpuHooks = rule == 1 ? kCpuHostCentreOnly : 0;
		HostRun r;
		if( !runOnHostBins( p, 48000, wavAt, seconds, r, rule == 1 ? 44100 : 48000 ) )
			return 1;
		std::string perBand;
		for( int b = 0; b < r.bandCount; ++b )
			perBand += fmt( "%s%zu", b == 0 ? "" : " / ", r.band[ b ].size() );
		int together = 0;
		for( double t : r.band[ 0 ] )
			together += std::find( r.band[ 1 ].begin(), r.band[ 1 ].end(), t ) != r.band[ 1 ].end();
		std::printf( "  %-44s onsets by band (40-160 / 160-640 / 640-2560 / 2560-8000 Hz) %s; %zu triggers (%.2f a second); "
		             "%d of the lowest band's onsets on a frame 160-640 Hz fired too\n",
		             rule == 0 ? "v0.1.1 (earshot 02306da, bins shared, 48 kHz):" : "v0.1.0 (earshot 8124191, bin to its centre):", perBand.c_str(),
		             r.triggers.size(), static_cast< double >( r.triggers.size() ) / seconds, together );
	}
	return 0;
}
