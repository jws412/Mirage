# Mirage
## Overview
This game prototype was made for the 2025 edition of <a href='https://athackctf.com/' target="new">@Hack</a>. A player faces an obstacle course with some enemies that attack at a distance. The player can slide and jump to maneuver around these enemies. An interesting feature awaits the player at the end of the level. The controls of this game are as follows:
 - Left and Right for moving horizontally,
 - Spacebar for jumping
 - Down for crouching,
 - The X key for running,
 - The C key for sliding,
 - Ctrl + D for toggling the visibility of the debug menu,
 - Ctrl + W for quitting the game.
## File Structure
The game expects the `user` and `enemy` folders to exist in its directory. The former contains information about the following game features:
 - The tilemap defining the terrain of the introductory level, in `user\Abe\tuto.lvl`,
 - The entity generation in the initial level, in `user\Abe\tuto.gen`,
 - The characteristics that all entities of a same type share, in `user\Mukki\moldInfo.txt`,
 - The tile atlas, in `user\Mukki\atlas.cfg`.

The `enemy` folder contains all graphic data for each entity as `.cfg` files. The `.cfg` file format only exists to obfuscate graphic data by compressing it.
## Compiling
The `b.bat` file contains a script for compiling all source code using GCC. The script interprets its first argument as the target level of optimisation. This project used MinGW throughout the entirety of development. The only prerequisite for compilation is to have access to the Windows API.
