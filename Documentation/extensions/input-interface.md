## Purpose

Clients have different platform-depended input code now.
It is bad because we cannot use same functions (if we won't rewrite almost half of SDL) on different platforms.

## Client part

* Client will have ability to fully implement touch input. Drawing may be done by HUD.
* Client will receive basic motion and look events from engine

### Client implementation

#### Client will optionally export some functions to Engine:
* `int IN_ClientTouchEvent ( int fingerID, float x, float y, float dx, float dy );`

Return 1 if touch is active, 0 otherwise.

* `void IN_ClientMoveEvent ( float forwardmove, float sidemove );`

Client wil accumulate move values before creating commands and flush it on CreateMove.

* `void IN_ClientLookEvent ( float relyaw, float relpitch );`

Client will rotate camera when needed as in mouse implementation

## Engine part

* Engine will handle platform events and call client functions.
* Engine will implement fallback look and movement system when client interface not present

### Engine implementation

#### Touch events

Before calling ClientMove engine must get touch events.

If client exported IN_ClientTouchEvent, event will be sent to client.

Otherwise engine will draw own touch interface.

#### Other events

Engine touch interface and joystick support code will generate two types of events:
* Move events (IN_ClientMoveEvent function)
* Look events (IN_ClientLookEvent function)

If client exported these functions, events will be sent to client before CreateMove
Otherwise Look Event will be processed before CreateMove, but MoveEvent after. It will be applied to generated command
