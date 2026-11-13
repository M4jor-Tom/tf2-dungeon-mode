# TF2 Dungeon Mode (experiment)

A fork of Valve's Source SDK 2013 (which includes the TF2 game code) where weapons are gone, players cast
spells, and the game is PvE: clear floors of monsters, together.

| Input | Spell |
|---|---|
| MOUSE1 | Fireball (0.6s cooldown) |
| hold MOUSE2, release | Ka-me-ha-me-ha: charge up to 1.5s, piercing beam, up to 400 damage |
| R | Lightning orb (10s cooldown) |
| MOUSE3 | Overheal (15s cooldown) |

- Players are forced onto RED. A director spawns floors of skeletons around the party; every 3rd floor
  adds a boss (Skeleton King, Horseless Headless Horsemann, Monoculus, in rotation).
- Clearing a floor revives fallen allies. A party wipe resets the run to floor 1. Spawn rooms are safe rooms:
  monsters can't path into them.
- Map objectives are disabled. No dungeon maps ship with TF2, so `cfg/mapcycle.txt` uses the Halloween maps.
  Maps without a nav mesh get one generated automatically (the map reloads once).
- Code: `src/game/server/tf/tf_dungeon.cpp` (spells + director), plus small hooks in `tf_player.cpp`
  (no weapons, spell input), `tf_gamerules.cpp` (`tf_dungeon_mode`, no captures, precache),
  `tf_weapon_spellbook.cpp` (fireball direct hits damage monsters), `tf_hud_crosshair.cpp` (weaponless crosshair).
- ConVars: `tf_dungeon_mode`, `tf_dungeon_base_monsters`, `tf_dungeon_max_monsters`, `tf_dungeon_boss_every`,
  `tf_dungeon_floor_delay`, `tf_dungeon_beam_damage`. Commands: `tf_dungeon_status`, `tf_dungeon_set_floor N` (cheat).

**Build on NixOS:** `nix develop`, then `cd src && bash ./buildallprojects` (podman + the Steam Runtime sniper image).
**Smoke test:** `tools/smoke_setup.sh` once (downloads dedicated-server content), then `python3 tools/smoke_test.py`.

---

# Source SDK 2013

Source code for Source SDK 2013.

Contains the game code for Half-Life 2, HL2: DM and TF2.

**Now including Team Fortress 2! ✨**

## Build instructions

Clone the repository using the following command:

`git clone https://github.com/ValveSoftware/source-sdk-2013`

### Windows

Requirements:
 - Source SDK 2013 Multiplayer installed via Steam
 - Visual Studio 2022 with the following workload and components:
   - Desktop development with C++:
     - MSVC v143 - VS 2022 C++ x64/x86 build tools (Latest)
     - Windows 11 SDK (10.0.22621.0) or Windows 10 SDK (10.0.19041.1)
 - Python 3.13 or later

Inside the cloned directory, navigate to `src`, run:
```bat
createallprojects.bat
```
This will generate the Visual Studio project `everything.sln` which will be used to build your mod.

Then, on the menu bar, go to `Build > Build Solution`, and wait for everything to build.

You can then select the `Client (Mod Name)` project you wish to run, right click and select `Set as Startup Project` and hit the big green `> Local Windows Debugger` button on the tool bar in order to launch your mod.

The default launch options should be already filled in for the `Release` configuration.

### Linux

Requirements:
 - Source SDK 2013 Multiplayer installed via Steam
 - podman

Inside the cloned directory, navigate to `src`, run:
```bash
./buildallprojects
```

This will build all the projects related to the SDK and your mods automatically against the Steam Runtime.

You can then, in the root of the cloned directory, you can navigate to `game` and run your mod by launching the build launcher for your mod project, eg:
```bash
./mod_tf
```

*Mods that are distributed on Steam MUST be built against the Steam Runtime, which the above steps will automatically do for you.*

## Distributing your Mod

There is guidance on distributing your mod both on and off Steam available at the following link:

https://partner.steamgames.com/doc/sdk/uploading/distributing_source_engine

## Additional Resources

- [Valve Developer Wiki](https://developer.valvesoftware.com/wiki/Source_SDK_2013)

## License

The SDK is licensed to users on a non-commercial basis under the [SOURCE 1 SDK LICENSE](LICENSE), which is contained in the [LICENSE](LICENSE) file in the root of the repository.

For more information, see [Distributing your Mod](#markdown-header-distributing-your-mod).
