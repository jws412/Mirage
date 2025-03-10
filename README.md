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
## Compiling
The `b.bat` file contains a script for compiling all source code using GCC. The script interprets its first argument as the target level of optimisation. This project used MinGW throughout the entirety of development. The only prerequisite for compilation is to have access to the Windows API.
