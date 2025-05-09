# Login Flow

This note explains the sequence of steps that occur when a user logs in:

1. **Client → Server**: The client submits credentials (`POST /login`).  
2. **Server**: Validates against the user database (`core/auth.c`).  
3. **Server → Client**: On success, sets a secure session cookie and returns `200 OK`.  
4. **Client**: Stores the cookie and proceeds to authenticated routes.

Below is the corresponding PlantUML diagram illustrating the message exchange:

*(The “Login Flow” diagram will render here automatically.)*

You can find the source for this diagram in `build_notes/diagrams/login_flow.puml`.
