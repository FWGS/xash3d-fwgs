# GameCube Networking Notes

Goal:

- Single-player must work offline. The local client/server flow may need
  loopback behavior, but the port must not depend on HTTP, TLS, master servers,
  or external networking.

Current guidance:

- Keep HTTP initialization disabled on GameCube unless a later goal proves a
  safe, bounded use case.
- Avoid UDP port binding or external network setup for normal offline boot.
- If loopback is needed, use the engine's existing abstractions in a local-only
  mode and document the exact behavior.

Verify:

- Boot without network services.
- Client/server spawn.
- Disconnect/shutdown.
- Changelevel and save/load once those goals are active.
