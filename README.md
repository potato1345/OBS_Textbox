# OBS_Textbox

OBS browser view text field that can be customized via the "Custom CSS" tab — with external live control through a web dashboard.

## Preview

![OBS Textbox Preview](preview.png)

## How It Works

| File | Purpose |
|------|---------|
| `textbox.html` | The OBS Browser Source overlay — shows text from CSS variables |
| `controlui/control.html` | A browser dashboard (OBS Custom Dock) to control multiple textboxes simultaneously |
| `start_server.bat` / `.sh` | Lightweight local web server scripts to run the dashboard |

The dashboard talks to OBS over the **OBS WebSocket server** (built into OBS 28+). Each time you type in a card, the dashboard rewrites the *Custom CSS* of the matching browser source — text and colors update live. All state is saved in the browser's local storage and survives OBS restarts; if the connection drops, the dashboard reconnects automatically.

While the core text system works offline and serverless, using advanced features like **AI PDF Import** requires the dashboard to be served via the local web server (`start_server` scripts) because the OBS browser strictly blocks external API requests from local `file:///` paths.

## Setup

### 1. Enable the OBS WebSocket Server

1. In OBS, go to **Tools -> WebSocket Server Settings**.
2. Check **Enable WebSocket server** and optionally set a **Password**.
3. Click OK and (if asked) restart OBS.

### 2. Start the Local Server & Add Dashboard to OBS

*To ensure all features (including the AI PDF Import) work without browser security errors, you should run the dashboard via the local web server.*

1. Run the included start script for your OS (requires Python):
   - **Windows:** Double-click `start_server.bat`
   - **Mac/Linux:** Open terminal and run `./start_server.sh`
   *(This starts a local background server on port 8080. Leave the black console window open while streaming).*
2. In OBS, go to **Docks** -> **Custom Browser Docks...**
3. Name the dock (e.g., "Textbox Control").
4. For the URL, enter exactly: `http://localhost:8080/controlui/control.html`
5. Apply and place the dock anywhere in your OBS UI.
6. Open **Einstellungen** in the dashboard, enter the WebSocket password and click **Mit OBS verbinden** (auto-reconnect is on by default).

> **Offline Mode (No AI PDF Import):** If you don't need the AI PDF features, you can skip starting the server and just enter the local file path directly as the URL (e.g. `file:///C:/Path/To/OBS_Textbox/controlui/control.html`).

### 3. Add the Textbox to OBS

1. Add a new **Browser Source** in OBS.
2. Check the **"Local file"** box and select the `textbox.html` file from this folder.
3. Set the dimensions (e.g. Width: 800, Height: 300)
4. Scroll down to the **"Custom CSS"** field.
5. Define your `--box-id` exactly like this:

```css
:root {
    --box-id: "lowerthird";
}

body { background-color: rgba(0, 0, 0, 0); margin: 0px auto; overflow: hidden; }
```

### 4. Open the Control Dashboard

1. Use the dock you added in step 2.
2. You'll see a card for each textbox you want to control.
3. Set each card's **ID** to match the `--box-id` configured in OBS (e.g., `lowerthird`).
4. Type your text and the OBS overlay updates instantly — with **Auto-Send** (default) each change is pushed automatically, or use **Senden** / **Alle senden** per card.

## Profiles & Saving

- The dashboard automatically saves your inputs, custom profiles, and active cards directly in the local storage of OBS. Everything is restored even after a PC restart or OBS relaunch.
- **Schnell-Profile**: save the full state of all boxes under a name (top-right chips) and reload it with one click.
- **Farbprofile**: the settings dialog contains default dashboard color profiles (plus YAML import/export and your own profiles) — independent of the textbox content.
- **Export / Import** (JSON) moves the complete state between machines.

## Advanced CSS Customization

You can fully customize the look directly in OBS via the **"Custom CSS"** field. Just add these variables inside the `:root` block:

| Variable | Description | Default |
|----------|-------------|---------|
| `--box-bg` | Background color of the box | `rgba(255, 255, 255, 0.75)` |
| `--box-border` | Border color | `rgba(0, 0, 0, 0.7)` |
| `--triangle-color-a` | Top-left corner accent | `#fce647` |
| `--triangle-color-b` | Bottom-right corner accent | `#fce647` |
| `--text-color` | Text color | `#000000` |
| `--zeile-1` | Text line 1 (can also be set statically here) | `""` |
| `--zeile-2` | Text line 2 (can also be set statically here) | `""` |
| `--zeile-3` | Text line 3 (can also be set statically here) | `""` |

Notes:

- The dashboard also manages `--box-opacity` via WebSocket: an empty box (all three lines cleared) fades out automatically.
- Empty text lines are hidden automatically, so the box shrinks to the visible content.

Example of a fully customized box in OBS:
```css
:root {
    --box-id: "interview";
    --box-bg: rgba(20, 20, 20, 0.9);
    --text-color: #ffffff;
    --triangle-color-a: #ff0055;
    --triangle-color-b: #ff0055;
}

body { background-color: rgba(0, 0, 0, 0); margin: 0px auto; overflow: hidden; }
```

## Troubleshooting

- **"Verbindung fehlgeschlagen"** — check that the WebSocket server is enabled, OBS is running, and the password in the dashboard matches (`Tools -> WebSocket Server Settings`).
- **Nothing updates in OBS** — make sure the browser source's Custom CSS contains `--box-id: "…"` (with quotes) and that the card ID in the dashboard matches exactly.
- **OBS restarts** — the dashboard reconnects automatically after ~3 seconds and re-applies all boxes.
