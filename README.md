# OBS_Textbox

OBS browser view text field that can be customized via the "Custom CSS" tab.

## Preview

You can view the HTML structure directly below. To use it, open `textbox.html` in your browser or copy the code into an OBS Browser source.

```html
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>OBS Textbox</title>
    <style>
        :root {
            --zeile-1: "";
            --zeile-2: "";
            --zeile-3: "";
            --box-bg: rgba(255, 255, 255, 0.75);
            --box-border: rgba(0, 0, 0, 0.7);
            --triangle-color: #fce647;
            --text-color: #000000;
        }

        body {
            background: transparent;
            margin: 0;
            padding: 20px;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        }

        .textbox-container {
            position: relative;
            background-color: var(--box-bg);
            background-image:
                linear-gradient(135deg, var(--triangle-color) 50%, transparent 50%),
                linear-gradient(315deg, var(--triangle-color) 50%, transparent 50%);
            background-position: top left, bottom right;
            background-size: 50px 50px, 50px 50px;
            background-repeat: no-repeat;
            border: 0px solid var(--box-border);
            border-radius: 4px;
            min-width: 300px;
            width: max-content;
            max-width: 800px;
            min-height: 1.5em;
            padding: 15px 60px 20px 50px;
            display: flex;
            flex-direction: column;
            justify-content: center;
        }

        .text-line {
            line-height: 1.2;
            color: var(--text-color);
        }

        .text-line::before {
            display: inline-block;
            white-space: pre-wrap;
            font-size: 32px;
        }

        .line1::before { content: var(--zeile-1); }
        .line2::before { content: var(--zeile-2); }
        .line3::before { content: var(--zeile-3); }

        .line1::before {
            font-size: 38px;
            font-weight: 500;
            margin-bottom: 5px;
        }

        .line2::before {
            font-size: 32px;
            font-weight: 400;
        }

        .line3::before {
            font-size: 32px;
            font-weight: 400;
        }
    </style>
</head>
<body>
    <div class="textbox-container">
        <div class="text-line line1"></div>
        <div class="text-line line2"></div>
        <div class="text-line line3"></div>
    </div>
</body>
</html>
```
