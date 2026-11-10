# Collaborative drawing language

## Language

**Board**:
A particular drawable screen within a particular area instance. Two screens, or
two instances of the same room, have independent boards.
_Avoid_: Texture name as board identity

**Canvas**:
The shared drawing belonging to a board, visible to players in that board's area.
_Avoid_: Player-private drawing

**Stroke**:
A player's individual freehand mark on a canvas, with its chosen color and thickness.

**Permanent board**:
A board whose drawing survives players leaving and server restarts.

**Instance board**:
A board whose drawing lasts until its dungeon instance is destroyed.

**Own stroke**:
A stroke belonging to its original character author, including after that character
reconnects. Another character on the same account is a different author.

**Stroke eraser**:
A tool that removes an entire eligible stroke touched by the user, rather than
removing individual pixels. Players may erase their own strokes; DMs may erase any.

**Clear recovery**:
A DM's ability to restore the drawing removed by the most recent clear.
