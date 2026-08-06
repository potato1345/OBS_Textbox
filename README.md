# OBS_Textbox

OBS browser view text field that can be customized via the "Custom CSS" tab — with external live control through a web dashboard.

## Preview

![OBS Textbox Preview](preview.png)

## How It Works

The system is now completely serverless and runs entirely within OBS:
| File | Purpose |
|------|---------|
| `textbox.html` | The OBS Browser Source overlay — shows text from CSS variables |
| `controlui/control.html` | A browser dashboard (OBS Custom Dock) to control multiple textboxes simultaneously |

The dashboard saves all text locally in the browser storage and uses `BroadcastChannel` to update the OBS overlays instantly in real-time. No Python server required!

## Setup

### 1. Add the Control Dashboard to OBS

1. In OBS, go to **Docks** -> **Custom Browser Docks...**
2. Name the dock (e.g., "Textbox Control").
3. For the URL, enter the local, absolute file path to the `controlui/control.html` file, formatted as a file URL. Example: `file:///Users/deinName/Pfad/Zu/OBS_Textbox/controlui/control.html` (Windows: `file:///C:/Pfad/Zu/OBS_Textbox/controlui/control.html`)
4. Apply and place the dock anywhere in your OBS UI.

### 2. Add the Textbox to OBS

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

### 3. Open the Control Dashboard

1. Use the dock you added in step 1.
2. You'll see a card for each textbox you want to control.
3. Set each card's **ID** to match the `--box-id` configured in OBS (e.g., `lowerthird`).
4. Type your text and the OBS overlay updates instantly via local BroadcastChannel sync!

## Profiles & Saving

The dashboard automatically saves your inputs, custom profiles, and active cards directly in the local storage of OBS. Everything is restored perfectly even after a PC restart or OBS relaunch.

## Advanced CSS Customization

You can fully customize the look directly in OBS via the **"Custom CSS"** field. Just add these variables inside the `:root` block:

| Variable | Description | Default |
|----------|-------------|---------|
| `--box-bg` | Background color of the box | `rgba(255, 255, 255, 0.75)` |
| `--box-border` | Border color | `rgba(0, 0, 0, 0.7)` |
| `--triangle-color` | Color of the corner accents | `#fce647` |
| `--text-color` | Text color | `#000000` |
| `--zeile-1` | Text line 1 (can also be set statically here) | `""` |
| `--zeile-2` | Text line 2 (can also be set statically here) | `""` |
| `--zeile-3` | Text line 3 (can also be set statically here) | `""` |

Example of a fully customized box in OBS:
```css
:root {
    --box-id: "interview";
    --box-bg: rgba(20, 20, 20, 0.9);
    --text-color: #ffffff;
    --triangle-color: #ff0055;
}

body { background-color: rgba(0, 0, 0, 0); margin: 0px auto; overflow: hidden; }
```
