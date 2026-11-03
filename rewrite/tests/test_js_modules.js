/**
 * Tests für die JavaScript-Module im Dashboard (mit Node.js ausgeführt).
 */

import assert from 'node:assert/strict';
import { hexToRgba, colorProfileToYAML, yamlToColorProfile, isDefaultColorProfile } from '../dashboard/js/color-profiles.js';
import { escapeCssString, buildInjectedCss, applyInjectedCss, INJECT_START_TAG, INJECT_END_TAG } from '../dashboard/js/css-builder.js';
import { parseLiedplanLocally, parseSongReference } from '../dashboard/js/pdf-ai-importer.js';

console.log('--- Test: color-profiles.js ---');
// 1. Hex to RGBA
assert.equal(hexToRgba('#ffffff', 255), 'rgb(255, 255, 255)');
assert.equal(hexToRgba('#000000', 0), 'rgba(0, 0, 0, 0)');
assert.equal(hexToRgba('#fce647', 128), 'rgba(252, 230, 71, 0.502)');
assert.equal(hexToRgba('#fff', 255), 'rgb(255, 255, 255)');

// 2. YAML Export & Import roundtrip
const testProfile = {
    name: 'Mein Testprofil',
    colors: {
        bgPrimary: '#112233', bgPrimaryOpacity: 240,
        bgSecondary: '#223344', bgSecondaryOpacity: 255,
        panelBg: '#334455', panelBgOpacity: 250,
        border: '#445566', borderOpacity: 255,
        cardOuterBg: '#445566', cardOuterBgOpacity: 255,
        panelBgAlt: '#556677', panelBgAltOpacity: 255,
        btnHoverBg: '#556677', btnHoverBgOpacity: 255,
        textColor: '#ffffff', textColorOpacity: 255,
        accentBlue: '#00aaff', accentBlueOpacity: 255
    }
};

const yamlStr = colorProfileToYAML(testProfile);
assert.match(yamlStr, /name:\s*"Mein Testprofil"/);
const parsedProfile = yamlToColorProfile(yamlStr);
assert.equal(parsedProfile.name, testProfile.name);
assert.equal(parsedProfile.colors.bgPrimary, '#112233');
assert.equal(parsedProfile.colors.bgPrimaryOpacity, 240);

// 3. Default Profile check
assert.equal(isDefaultColorProfile('OBS Dark'), true);
assert.equal(isDefaultColorProfile('obs dark'), true);
assert.equal(isDefaultColorProfile('Mein Testprofil'), false);

console.log('--- Test: css-builder.js ---');
// 4. CSS String Escaping
assert.equal(escapeCssString('Test "Anführungszeichen" & Backslash \\'), 'Test \\"Anführungszeichen\\" & Backslash \\\\');

// 5. Build Injected CSS
const box = {
    id: 'lowerthird',
    zeile1: 'Gottesdienst',
    zeile2: '144, 1-3',
    zeile3: 'Nun jauchzt dem Herren',
    boxColor: '#ffffff',
    boxColorOpacity: 255,
    cornerColorA: '#fce647',
    cornerColorAOpacity: 255,
    cornerColorB: '#fce647',
    cornerColorBOpacity: 255,
    textColor: '#000000',
    textColorOpacity: 255,
    rainbowCorners: false,
    animFile: 'stern.gif',
    showAnim: true,
    animScale: 4,
    animX: 10,
    animY: -20
};

const css = buildInjectedCss(box);
assert.match(css, new RegExp(INJECT_START_TAG.replace(/[*\\/]/g, '\\$&')));
assert.match(css, new RegExp(INJECT_END_TAG.replace(/[*\\/]/g, '\\$&')));
assert.match(css, /--zeile-1:\s*"Gottesdienst"/);
assert.match(css, /--zeile-2:\s*"144, 1-3"/);
assert.match(css, /--show-anim:\s*1/);
assert.match(css, /--anim-scale:\s*4/);
assert.match(css, /--box-opacity:\s*1/);

// Empty box check
const emptyBox = { id: 'test', zeile1: '', zeile2: '', zeile3: '' };
const emptyCss = buildInjectedCss(emptyBox);
assert.match(emptyCss, /--box-opacity:\s*0/);

// 6. Apply Injected CSS (replaces existing block)
const initialObsCss = `
:root { --box-id: "lowerthird"; }
body { margin: 0; }
${css}
`;
const updatedCss = applyInjectedCss(initialObsCss, emptyCss);
assert.equal(updatedCss.includes(INJECT_START_TAG), true);
// Count injection start tags - must be exactly 1
const occurrences = (updatedCss.match(new RegExp(INJECT_START_TAG.replace(/[*\\/]/g, '\\$&'), 'g')) || []).length;
assert.equal(occurrences, 1, 'Injected block must not be duplicated');

console.log('--- Test: pdf-ai-importer.js (local fallback parser) ---');
const sampleText = `
Liedplan St. Johannes
Woche vom 20.09.2026
Sonntag
Eröffnung 144, 1+4+6 Nun jauchzt dem Herren
Kyrie 437, 1-4 Meine engen Grenzen
Gloria 172
Kommunion Orgel
Schlusslied 868, 1+6 Ave Maria klare
`;

const items = parseLiedplanLocally(sampleText);
assert.ok(items.length >= 4, `Erwartet mindestens 4 Einträge, erhalten: ${items.length}`);
assert.equal(items[0].zeile1, 'Eröffnung');
assert.equal(items[0].zeile2, '144, 1+4+6');
assert.equal(items[0].zeile3, 'Nun jauchzt dem Herren');

console.log('--- Test: pdf-ai-importer.js (lieder.md-Referenzparser) ---');
const liederSample = `# Gotteslob-Referenztabelle

| Nr    | Titel | altes GL | Rubrik | Unterrubrik | ökumenisch |
| ----- | ----- | -------- | ------ | ----------- | ---------- |
| 31,1  | Selig der Mensch, der seine Freude hat (Kv zu Ps 1) | 710,1 | Psalmen | Kehrverse |  |
| 144   | Nun jauchzt dem Herren, aller Welt mit Freuden |       | Lieder zur Einsegnung |  |  |
`;
const songRef = parseSongReference(liederSample);
assert.equal(songRef['31,1'].title, 'Selig der Mensch, der seine Freude hat (Kv zu Ps 1)');
assert.equal(songRef['31,1'].rubrik, 'Psalmen');
assert.equal(songRef['144'].title, 'Nun jauchzt dem Herren, aller Welt mit Freuden');
assert.equal(Object.keys(songRef).length, 2, 'Genau 2 Lieder erwartet (kein Header/Separator)');
assert.ok(!songRef['Nr'], 'Header-Zeile darf nicht als Lied indiziert werden');

// Robustheit: Titelzeile VOR der Tabelle + ohne Header-Zeile → Parsing ab Zeile 0
const liederNoHeader = `Kleine Testliste
| 5 | Ein Testlied |  | Lieder |  |  |
| --- | --- | --- | --- | --- | --- |
| 6 | Zweites Lied |  | Lieder |  |  |
`;
const songRef2 = parseSongReference(liederNoHeader);
assert.equal(songRef2['5'].title, 'Ein Testlied');
assert.equal(songRef2['6'].title, 'Zweites Lied');
assert.equal(Object.keys(songRef2).length, 2, 'Separator-Zeile muss übersprungen werden');

console.log('✅ Alle JavaScript-Modultests erfolgreich abgeschlossen.');
