
namespace bspguy {
	array<dictionary> g_ent_defs;
	bool noscript = false; // true if this script shouldn't be used but is loaded anyway
	
	dictionary no_delete_ents; // entity classes that don't work right if spawned late
	dictionary map_loaded;
	dictionary map_cleaned;
	array<string> map_order;
	int current_map_idx;

	void print(string text) { g_Game.AlertMessage( at_console, text); }
	void println(string text) { print(text + "\n"); }
	
	void delay_respawn() {
		g_PlayerFuncs.RespawnAllPlayers(true, true);
	}
	
	void mapchange_internal(string thisMap, string nextMap) {
		for (uint i = 0; i < map_order.size(); i++) {
			if (map_order[i] == nextMap) {
				current_map_idx = i;
				break;
			}
		}
		
		println("Transition from " + thisMap + " to " + nextMap);
		
		if (thisMap != nextMap) {
			spawnMapEnts(nextMap);
			deleteMapEnts(thisMap, false, true); // delete spawns immediately
			g_Scheduler.SetTimeout("delay_respawn", 0.5f);
			g_Scheduler.SetTimeout("deleteMapEnts", 1.0f, thisMap, false, false); // delete everything else
		} else {
			deleteMapEnts(thisMap, false, false);
			spawnMapEnts(nextMap);
			g_Scheduler.SetTimeout("delay_respawn", 0.5f);
		}
	}
	
	void mapchange(CBaseEntity@ pActivator, CBaseEntity@ pCaller, USE_TYPE useType, float flValue)
	{
		string thisMap = getCustomStringKeyvalue(pCaller, "$s_bspguy_map_source");
		string nextMap = getCustomStringKeyvalue(pCaller, "$s_next_map");
		
		if (map_cleaned.exists(thisMap)) {
			println("Map " + nextMap + " has already been cleaned. Ignoring mapchange trigger.");
			return;
		}
		if (map_loaded.exists(nextMap)) {
			println("Map " + nextMap + " has already loaded. Ignoring mapchange trigger.");
			return;
		}
		map_cleaned[thisMap] = true;
		map_loaded[nextMap] = true;
		
		mapchange_internal(thisMap, nextMap);
	}
	
	void mapload(CBaseEntity@ pActivator, CBaseEntity@ pCaller, USE_TYPE useType, float flValue)
	{
		string nextMap = getCustomStringKeyvalue(pCaller, "$s_next_map");
		
		if (map_loaded.exists(nextMap)) {
			println("Map " + nextMap + " has already loaded. Ignoring mapload trigger.");
			return;
		}
		map_loaded[nextMap] = true;
		
		println("Loading map " + nextMap);
		
		spawnMapEnts(nextMap);
	}
	
	void mapclean(CBaseEntity@ pActivator, CBaseEntity@ pCaller, USE_TYPE useType, float flValue)
	{
		string cleanMap = getCustomStringKeyvalue(pCaller, "$s_bspguy_map_source");
		
		if (map_cleaned.exists(cleanMap)) {
			println("Map " + cleanMap + " has already been cleaned. Ignoring mapclean trigger.");
			return;
		}
		map_cleaned[cleanMap] = true;
		
		println("Cleaning map " + cleanMap);
		
		deleteMapEnts(cleanMap, false, false); // delete everything
	}
	
	void loadMapEnts() {
		string entFileName = string(g_Engine.mapname).ToLowercase() + ".ent";
		string fpath = "scripts/maps/bspguy/maps/" + entFileName;
		File@ f = g_FileSystem.OpenFile( fpath, OpenFile::READ );
		if( f is null or !f.IsOpen())
		{
			println("ERROR: bspguy ent file not found: " + fpath);
			return;
		}

		int lineNum = 0;
		int lastBracket = -1;
		string line;
		
		dictionary current_ent;
		while( !f.EOFReached() )
		{
			f.ReadLine(line);
			
			lineNum++;
			if (line.Length() < 1 || line[0] == '\n')
				continue;

			if (line[0] == '{')
			{
				if (lastBracket == 0)
				{
					println(entFileName + " (line " + lineNum + "): Unexpected '{'");
					continue;
				}
				lastBracket = 0;
				current_ent = dictionary();
			}
			else if (line[0] == '}')
			{
				if (lastBracket == 1)
					println(entFileName + " (line " + lineNum + "): Unexpected '}'");
				lastBracket = 1;

				if (current_ent.isEmpty())
					continue;

				g_ent_defs.push_back(current_ent);
				current_ent = dictionary();
			}
			else if (lastBracket == 0) // currently defining an entity
			{
				// parse keyvalue
				int begin = -1;
				int end = -1;

				string key = "";
				string value = "";
				int comment = 0;

				for (uint i = 0; i < line.Length(); i++)
				{
					if (line[i] == '/')
					{
						if (++comment >= 2)
						{
							key = value = "";
							break;
						}
					}
					else
						comment = 0;
					if (line[i] == '"')
					{
						if (begin == -1)
							begin = i + 1;
						else
						{
							end = i;
							if (key.Length() == 0)
							{
								key = line.SubString(begin,end-begin);
								begin = end = -1;
							}
							else
							{
								value = line.SubString(begin,end-begin);
								break;
							}
						}
					}
				}
				
				if (key.Length() > 0) {
					current_ent[key] = value;
				}
			}
		}
		
		println("Loaded " + g_ent_defs.size() + " entity definitions from " + entFileName);
	}
	
	void deleteMapEnts(string mapName, bool invertFilter, bool spawnsOnly) {
	
		string infoEntName = "bspguy_info_" + mapName;
		CBaseEntity@ mapchangeEnt = g_EntityFuncs.FindEntityByTargetname(null, infoEntName);
		
		bool minMaxLoaded = false;
		Vector min, max;
		if (mapchangeEnt !is null) {
			min = getCustomVectorKeyvalue(mapchangeEnt, "$v_min");
			max = getCustomVectorKeyvalue(mapchangeEnt, "$v_max");
			minMaxLoaded = true;
		} else {
			println("ERROR: Missing entity '" + infoEntName + "'. Some entities may not be deleted in previous maps, and that can cause lag!");
		}
	
		CBaseEntity@ ent = null;
		do {
			@ent = g_EntityFuncs.FindEntityByClassname(ent, "*");
			if (ent !is null) {
				if (spawnsOnly && string(ent.pev.classname) != "info_player_deathmatch")
					continue;
			
				if (ent.IsPlayer() or string(ent.pev.targetname).Find("bspguy") == 0) {
					continue;
				}
				
				// don't remove player items/weapons
				CBasePlayerItem@ item = cast<CBasePlayerItem@>(ent);
				if (item !is null && item.m_hPlayer.IsValid()) {
					continue;
				}
				
				KeyValueBuffer@ pKeyvalues = g_EngineFuncs.GetInfoKeyBuffer( ent.edict() );
				CustomKeyvalues@ pCustom = ent.GetCustomKeyvalues();
				CustomKeyvalue mapKeyvalue( pCustom.GetKeyvalue( "$s_bspguy_map_source" ) );
				if (mapKeyvalue.Exists()) {
					string mapSource = mapKeyvalue.GetString();
					if (invertFilter && mapSource == mapName) {
						continue;
					} else if (!invertFilter && mapSource != mapName) {
						continue;
					}
				} else if (minMaxLoaded) {
					Vector ori = ent.pev.origin;
					// probably a entity that spawned from a squadmaker or something
					// skip if it's outside the map boundaries
					bool outOfBounds = ori.x < min.x || ori.x > max.x || ori.y < min.y || ori.y > max.y || ori.z < min.z || ori.z > max.z;
					if ((!invertFilter && outOfBounds) || (invertFilter && !outOfBounds)) {
						continue;
					}
				}
				
				if (no_delete_ents.exists(ent.pev.classname)) {
					continue;
				}
				
				g_EntityFuncs.Remove(ent);
			}
		} while (ent !is null);
	}
	
	void spawnMapEnts(string mapName) {
		for (uint i = 0; i < g_ent_defs.size(); i++) {
			string mapSource;
			g_ent_defs[i].get("$s_bspguy_map_source", mapSource);
			
			if (mapSource == mapName) {
				string classname;
				g_ent_defs[i].get("classname", classname);
				
				if (no_delete_ents.exists(classname)) {
					continue;
				}
				
				g_EntityFuncs.CreateEntity(classname, g_ent_defs[i], true);
			}
		}
	}
	
	CustomKeyvalue getCustomKeyvalue(CBaseEntity@ ent, string keyName) {
		KeyValueBuffer@ pKeyvalues = g_EngineFuncs.GetInfoKeyBuffer( ent.edict() );
		CustomKeyvalues@ pCustom = ent.GetCustomKeyvalues();
		return CustomKeyvalue( pCustom.GetKeyvalue( keyName ) );
	}
	
	string getCustomStringKeyvalue(CBaseEntity@ ent, string keyName) {
		CustomKeyvalue keyvalue = getCustomKeyvalue(ent, keyName);
		if (keyvalue.Exists()) {
			return keyvalue.GetString();
		}
		return "";
	}
	
	Vector getCustomVectorKeyvalue(CBaseEntity@ ent, string keyName) {
		CustomKeyvalue keyvalue = getCustomKeyvalue(ent, keyName);
		if (keyvalue.Exists()) {
			return keyvalue.GetVector();
		}
		return Vector(0,0,0);
	}
	
	void MapInit() {
		loadMapEnts();
		
		no_delete_ents["multi_manager"] = true; // never triggers anything if spawned late
	}
	
	void MapActivate() {		
		string firstMapName;
		CBaseEntity@ infoEnt = g_EntityFuncs.FindEntityByTargetname(null, "bspguy_info");
		if (infoEnt !is null) {
			firstMapName = getCustomStringKeyvalue(infoEnt, "$s_map0");
			current_map_idx = 0;
			
			for (int i = 0; i < 64; i++) {
				string mapName = getCustomStringKeyvalue(infoEnt, "$s_map" + i);
				if (mapName.Length() > 0)
					map_order.insertLast(mapName);
				else
					break;
			}
			
			noscript = getCustomStringKeyvalue(infoEnt, "$s_noscript") == "yes";
		} else {
			println("ERROR: Missing entity 'bspguy_info'. bspguy script disabled!");
			return;
		}
		
		if (noscript) {
			println("WARNING: this map was not intended to be used with the bspguy script!");
			return;
		}
		
		if (firstMapName.Length() == 0) {
			println("ERROR: bspguy_info entity has no $s_mapX keys. bspguy script disabled!");
			return;
		}
		
		dictionary keys;
		keys["targetname"] = "bspguy_mapchange";
		keys["delay"] = "0";
		keys["m_iszScriptFile"] = "bspguy/bspguy";
		keys["m_iszScriptFunctionName"] = "bspguy::mapchange";
		keys["m_iMode"] = "1"; // trigger
		g_EntityFuncs.CreateEntity("trigger_script", keys, true);
		
		keys["targetname"] = "bspguy_mapload";
		keys["delay"] = "0";
		keys["m_iszScriptFile"] = "bspguy/bspguy";
		keys["m_iszScriptFunctionName"] = "bspguy::mapload";
		keys["m_iMode"] = "1"; // trigger
		g_EntityFuncs.CreateEntity("trigger_script", keys, true);
		
		keys["targetname"] = "bspguy_mapclean";
		keys["delay"] = "0";
		keys["m_iszScriptFile"] = "bspguy/bspguy";
		keys["m_iszScriptFunctionName"] = "bspguy::mapclean";
		keys["m_iMode"] = "1"; // trigger
		g_EntityFuncs.CreateEntity("trigger_script", keys, true);
		
		// all entities in all sections are spawned by now. Delete everything except for the ents in the first section.
		// It may be a bit slow to spawn all ents at first, but that will ensure everything is precached
		deleteMapEnts(firstMapName, true, false);
	}

	void printMapSections(CBasePlayer@ plr) {
		g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, "Map sections:\n");	
		for (uint i = 0; i < map_order.size(); i++) {
			string begin = i < 9 ? "     " : "    ";
			string end = i == uint(current_map_idx) ? "    (CURRENT SECTION)\n" : "\n";
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, begin + (i+1) + ") " +  map_order[i] + end);
		}
	}

	void doCommand(CBasePlayer@ plr, const CCommand@ args, bool inConsole) {
		bool isAdmin = g_PlayerFuncs.AdminLevel(plr) >= ADMIN_YES;
		
		if (args.ArgC() >= 2)
		{
			if (args[1] == "version") {
				g_PlayerFuncs.SayText(plr, "bspguy script v1\n");
			}
			if (args[1] == "list") {
				printMapSections(plr);
			}
			if (args[1] == "mapchange") {
				if (!isAdmin) {
					g_PlayerFuncs.SayText(plr, "Only admins can use that command.\n");
					return;
				}
				if (args.ArgC() >= 3) {
					string arg = args[2];
					string thisMap = map_order[current_map_idx];
					string nextMap;
					for (uint i = 0; i < map_order.size(); i++) {
						if (arg.ToLowercase() == map_order[i].ToLowercase()) {
							nextMap = arg;
							break;
						}
					}
					if (nextMap.Length() == 0) {
						uint idx = atoi(arg) - 1;
						if (idx < map_order.size()) {
							nextMap = map_order[idx];
						}
					}
					if (nextMap.Length() == 0) {
						g_PlayerFuncs.SayText(plr, "Invalid section name/number. See \"bspguy list\" output.\n");
					} else {
						mapchange_internal(thisMap, nextMap);
						printMapSections(plr);
					}
				} else {
					if (current_map_idx >= int(map_order.size())-1) {
						g_PlayerFuncs.SayText(plr, "This is the last map section.\n");
					} else {
						string thisMap = map_order[current_map_idx];
						string nextMap = map_order[current_map_idx+1];
						
						mapchange_internal(thisMap, nextMap);
						printMapSections(plr);
					}
				}
			}
		} else {			
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, '----------------------------------bspguy commands----------------------------------\n\n');
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, 'Type "bspguy list" to list map sections.\n\n');
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, 'Type "bspguy mapchange [name|number]" to transition to a new map section.\n');
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, '    [name|number] = Optional. Map section name or number to load (as shown in "bspguy list")\n');
			g_PlayerFuncs.ClientPrint(plr, HUD_PRINTCONSOLE, '\n-----------------------------------------------------------------------------------\n\n');
		}
	}

	CClientCommand _bspguy("bspguy", "bspguy commands", @bspguy::consoleCmd );

	void consoleCmd( const CCommand@ args ) {
		CBasePlayer@ plr = g_ConCommandSystem.GetCurrentPlayer();
		doCommand(plr, args, true);
	}
}

