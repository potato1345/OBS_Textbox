/**
 * @file cards.js
 * Verwaltung, Rendering und Interaktion der Textbox-Karten im Dashboard.
 * Eingeklappt zeigen die Karten kompakt nur die drei Textzeilen und die Aktionsbuttons.
 */

import { getState, updateState } from './storage.js';
import { pushStateToOBS } from './obs-sync.js';
import { showToast } from './toast.js';
import { createDefaultBox, uniqueBoxId } from './box-utils.js';

let debounceTimer = null;

function triggerAutoSend() {
    const state = getState();
    if (!state.autoSend) return;

    clearTimeout(debounceTimer);
    debounceTimer = setTimeout(() => {
        pushStateToOBS();
    }, 140);
}

/**
 * Erzeugt das HTML für eine einzelne Karte.
 */
export function createCardElement(box, index) {
    const card = document.createElement('div');
    card.className = 'card-outer';
    card.dataset.boxId = box.id;

    card.innerHTML = `
        <div class="card-id-header">
            <span class="card-id-badge">ID</span>
            <input type="text" class="card-id-input" value="${escapeAttr(box.id)}" placeholder="box_id" title="Diese ID muss mit --box-id in OBS übereinstimmen" aria-label="Box ID">
            <button type="button" class="btn-icon-subtle btn-delete-card" title="Karte entfernen" aria-label="Karte entfernen">✕</button>
        </div>
        <div class="card-inner">
            <div class="card-text-fields">
                <div class="field-row">
                    <label class="field-label-small">Zeile 1</label>
                    <input type="text" class="text-input input-z1" value="${escapeAttr(box.zeile1 || '')}" placeholder="Haupttext..." aria-label="Zeile 1">
                </div>
                <div class="field-row">
                    <label class="field-label-small">Zeile 2</label>
                    <input type="text" class="text-input input-z2" value="${escapeAttr(box.zeile2 || '')}" placeholder="Untertext..." aria-label="Zeile 2">
                </div>
                <div class="field-row">
                    <label class="field-label-small">Zeile 3</label>
                    <input type="text" class="text-input input-z3" value="${escapeAttr(box.zeile3 || '')}" placeholder="Zusatz..." aria-label="Zeile 3">
                </div>
            </div>

            <!-- Ausklappbereich: Animation -->
            <details class="card-dropdown">
                <summary class="dropdown-summary">🎬 Animation</summary>
                <div class="dropdown-body">
                    <div class="field-row">
                        <label class="field-label-small">Dateiname</label>
                        <input type="text" class="text-input input-anim-file" value="${escapeAttr(box.animFile || '')}" placeholder="z. B. stern.gif oder feuer.webm" title="Muss im Ordner OBS_Textbox oder als URL verfügbar sein">
                    </div>
                    <div class="slider-row">
                        <div class="slider-col">
                            <div class="slider-header">
                                <label class="field-label-small">X-Versatz</label>
                                <span class="val-display val-anim-x">${box.animX || 0}px</span>
                            </div>
                            <input type="range" class="slider input-anim-x" min="-1200" max="1200" step="5" value="${box.animX || 0}">
                        </div>
                        <div class="slider-col">
                            <div class="slider-header">
                                <label class="field-label-small">Y-Versatz</label>
                                <span class="val-display val-anim-y">${box.animY || 0}px</span>
                            </div>
                            <input type="range" class="slider input-anim-y" min="-1200" max="1200" step="5" value="${box.animY || 0}">
                        </div>
                    </div>
                    <div class="control-grid-2">
                        <div>
                            <label class="field-label-small">Skalierung</label>
                            <select class="select-input input-anim-scale">
                                ${[1, 2, 3, 4, 6, 8, 10, 12].map(scale => `
                                    <option value="${scale}" ${Number(box.animScale) === scale ? 'selected' : ''}>${scale}x</option>
                                `).join('')}
                            </select>
                        </div>
                        <div class="checkbox-wrapper">
                            <label class="checkbox-label">
                                <input type="checkbox" class="input-anim-show" ${box.showAnim ? 'checked' : ''}>
                                <span>Aktiv</span>
                            </label>
                        </div>
                    </div>
                </div>
            </details>

            <!-- Ausklappbereich: Farben & Deckkraft (Gemäß Spezifikation eingeklappt gehalten) -->
            <details class="card-dropdown">
                <summary class="dropdown-summary">🎨 Farben &amp; Deckkraft</summary>
                <div class="dropdown-body">
                    <div class="color-picker-pair">
                        <label class="field-label-small">Textfarbe</label>
                        <div class="color-inputs-group">
                            <input type="color" class="color-picker input-text-color" value="${box.textColor || '#000000'}" aria-label="Textfarbe">
                            <input type="number" class="opacity-number input-text-opacity" min="0" max="255" value="${box.textColorOpacity ?? 255}" title="Deckkraft 0-255" aria-label="Deckkraft Textfarbe">
                        </div>
                    </div>
                    <div class="color-picker-pair">
                        <label class="field-label-small">Box-Hintergrund</label>
                        <div class="color-inputs-group">
                            <input type="color" class="color-picker input-box-color" value="${box.boxColor || '#ffffff'}" aria-label="Hintergrundfarbe">
                            <input type="number" class="opacity-number input-box-opacity" min="0" max="255" value="${box.boxColorOpacity ?? 255}" title="Deckkraft 0-255" aria-label="Deckkraft Hintergrund">
                        </div>
                    </div>
                    <div class="corner-colors-group ${box.rainbowCorners ? 'is-rainbow' : ''}">
                        <div class="color-picker-pair">
                            <label class="field-label-small">Ecke Oben-Links</label>
                            <div class="color-inputs-group">
                                <input type="color" class="color-picker input-corner-a-color" value="${box.cornerColorA || '#fce647'}" aria-label="Eckenfarbe Oben-Links">
                                <input type="number" class="opacity-number input-corner-a-opacity" min="0" max="255" value="${box.cornerColorAOpacity ?? 255}" title="Deckkraft 0-255">
                            </div>
                        </div>
                        <div class="color-picker-pair">
                            <label class="field-label-small">Ecke Unten-Rechts</label>
                            <div class="color-inputs-group">
                                <input type="color" class="color-picker input-corner-b-color" value="${box.cornerColorB || '#fce647'}" aria-label="Eckenfarbe Unten-Rechts">
                                <input type="number" class="opacity-number input-corner-b-opacity" min="0" max="255" value="${box.cornerColorBOpacity ?? 255}" title="Deckkraft 0-255">
                            </div>
                        </div>
                    </div>
                    <div class="checkbox-wrapper">
                        <label class="checkbox-label">
                            <input type="checkbox" class="input-rainbow-toggle" ${box.rainbowCorners ? 'checked' : ''}>
                            <span>🌈 Regenbogen-Ecken (animiert)</span>
                        </label>
                    </div>
                </div>
            </details>

            <div class="card-footer-actions">
                <button type="button" class="btn btn-green btn-send-card" title="Diese Karte an OBS senden">
                    <span class="btn-icon">▶</span> Senden
                </button>
                <button type="button" class="btn btn-red btn-clear-card" title="Inhalt dieser Karte leeren">
                    <span class="btn-icon">✕</span> Leeren
                </button>
            </div>
        </div>
    `;

    bindCardEvents(card);
    return card;
}

function bindCardEvents(card) {
    // ID Input (mit Kollisionsprüfung, F3)
    const idInput = card.querySelector('.card-id-input');
    idInput.addEventListener('change', () => {
        const newId = idInput.value.trim() || 'box';
        const oldId = card.dataset.boxId;
        if (newId === oldId) return;
        const state = getState();
        if (state.boxes.some(x => x.id === newId && x.id !== oldId)) {
            showToast('error', `Box-ID "${newId}" existiert bereits.`);
            idInput.value = oldId;
            return;
        }
        updateState(state => {
            const b = state.boxes.find(x => x.id === oldId);
            if (b) b.id = newId;
        }, 'rename_box');
        card.dataset.boxId = newId;
        triggerAutoSend();
    });

    // Textzeilen
    const z1 = card.querySelector('.input-z1');
    const z2 = card.querySelector('.input-z2');
    const z3 = card.querySelector('.input-z3');

    const updateTexts = () => {
        updateState(state => {
            const b = state.boxes.find(x => x.id === card.dataset.boxId);
            if (b) {
                b.zeile1 = z1.value;
                b.zeile2 = z2.value;
                b.zeile3 = z3.value;
            }
        }, 'text_change');
        triggerAutoSend();
    };

    z1.addEventListener('input', updateTexts);
    z2.addEventListener('input', updateTexts);
    z3.addEventListener('input', updateTexts);

    // Animation Controls
    const animFile = card.querySelector('.input-anim-file');
    const animShow = card.querySelector('.input-anim-show');
    const animScale = card.querySelector('.input-anim-scale');
    const animX = card.querySelector('.input-anim-x');
    const animY = card.querySelector('.input-anim-y');
    const valX = card.querySelector('.val-anim-x');
    const valY = card.querySelector('.val-anim-y');

    const updateAnim = () => {
        updateState(state => {
            const b = state.boxes.find(x => x.id === card.dataset.boxId);
            if (b) {
                b.animFile = animFile.value.trim();
                b.showAnim = animShow.checked;
                b.animScale = Number(animScale.value) || 3;
                b.animX = Number(animX.value) || 0;
                b.animY = Number(animY.value) || 0;
            }
        }, 'anim_change');
        triggerAutoSend();
    };

    animFile.addEventListener('input', updateAnim);
    animShow.addEventListener('change', updateAnim);
    animScale.addEventListener('change', updateAnim);

    animX.addEventListener('input', () => {
        valX.textContent = `${animX.value}px`;
        updateAnim();
    });
    animY.addEventListener('input', () => {
        valY.textContent = `${animY.value}px`;
        updateAnim();
    });

    // Farb- und Opacity Controls
    const textColor = card.querySelector('.input-text-color');
    const textOpacity = card.querySelector('.input-text-opacity');
    const boxColor = card.querySelector('.input-box-color');
    const boxOpacity = card.querySelector('.input-box-opacity');
    const cornerA = card.querySelector('.input-corner-a-color');
    const cornerAOpacity = card.querySelector('.input-corner-a-opacity');
    const cornerB = card.querySelector('.input-corner-b-color');
    const cornerBOpacity = card.querySelector('.input-corner-b-opacity');
    const rainbowToggle = card.querySelector('.input-rainbow-toggle');
    const cornerGroup = card.querySelector('.corner-colors-group');

    const updateColors = () => {
        updateState(state => {
            const b = state.boxes.find(x => x.id === card.dataset.boxId);
            if (b) {
                b.textColor = textColor.value;
                b.textColorOpacity = clampOpacity(textOpacity.value);
                b.boxColor = boxColor.value;
                b.boxColorOpacity = clampOpacity(boxOpacity.value);
                b.cornerColorA = cornerA.value;
                b.cornerColorAOpacity = clampOpacity(cornerAOpacity.value);
                b.cornerColorB = cornerB.value;
                b.cornerColorBOpacity = clampOpacity(cornerBOpacity.value);
                b.rainbowCorners = rainbowToggle.checked;
            }
        }, 'color_change');

        cornerGroup.classList.toggle('is-rainbow', rainbowToggle.checked);
        triggerAutoSend();
    };

    [textColor, textOpacity, boxColor, boxOpacity, cornerA, cornerAOpacity, cornerB, cornerBOpacity].forEach(el => {
        el.addEventListener('input', updateColors);
    });
    rainbowToggle.addEventListener('change', updateColors);

    // Senden & Leeren
    const btnSend = card.querySelector('.btn-send-card');
    btnSend.addEventListener('click', async () => {
        const id = card.dataset.boxId;
        const res = await pushStateToOBS(id);
        if (res?.connected) {
            showToast('success', `"${id}" an OBS gesendet`);
        } else {
            showToast('warning', `OBS nicht verbunden – Änderungen lokal gespeichert`);
        }
    });

    const btnClear = card.querySelector('.btn-clear-card');
    btnClear.addEventListener('click', () => {
        z1.value = '';
        z2.value = '';
        z3.value = '';
        updateTexts();
        pushStateToOBS(card.dataset.boxId);
        showToast('info', `"${card.dataset.boxId}" geleert`);
    });

    // Löschen
    const btnDelete = card.querySelector('.btn-delete-card');
    btnDelete.addEventListener('click', () => {
        const id = card.dataset.boxId;
        card.style.transform = 'scale(0.92)';
        card.style.opacity = '0';
        setTimeout(() => {
            updateState(state => {
                state.boxes = state.boxes.filter(b => b.id !== id);
            }, 'delete_box');
            card.remove();
            // Blendet gelöschte Box in OBS aus
            pushStateToOBS(id);
            showToast('info', `Box "${id}" entfernt`);
        }, 200);
    });
}

function clampOpacity(val) {
    const num = Number(val);
    if (isNaN(num)) return 255;
    return Math.max(0, Math.min(255, Math.round(num)));
}

function escapeAttr(str) {
    return String(str || '')
        .replace(/&/g, '&amp;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
}

/**
 * Rendert alle Karten aus dem State in den Dashboard-Container.
 */
export function renderDashboardCards(container) {
    if (!container) return;
    const state = getState();
    container.replaceChildren();

    if (!state.boxes || state.boxes.length === 0) {
        const emptyHint = document.createElement('div');
        emptyHint.className = 'dashboard-empty-hint';
        emptyHint.innerHTML = `
            <p>Keine Textboxen vorhanden.</p>
            <button type="button" class="btn btn-accent" id="btn-empty-add-box">＋ Neue Box hinzufügen</button>
        `;
        emptyHint.querySelector('#btn-empty-add-box').addEventListener('click', addNewBox);
        container.appendChild(emptyHint);
        return;
    }

    state.boxes.forEach((box, idx) => {
        container.appendChild(createCardElement(box, idx));
    });
}

export function addNewBox(preferredId = null) {
    const state = getState();
    let id = preferredId;
    if (!id) {
        let counter = state.boxes.length + 1;
        while (state.boxes.some(b => b.id === `box${counter}`)) {
            counter++;
        }
        id = `box${counter}`;
    }

    // F4: Box-Factory zentral in box-utils.js
    const newBox = createDefaultBox(id);

    updateState(s => {
        s.boxes.push(newBox);
    }, 'add_box');

    const dashboard = document.getElementById('dashboard');
    if (dashboard) {
        renderDashboardCards(dashboard);
        const lastCard = dashboard.querySelector(`.card-outer[data-box-id="${id}"]`);
        if (lastCard) {
            lastCard.scrollIntoView({ behavior: 'smooth', block: 'nearest' });
        }
    }

    showToast('info', `Neue Box "${id}" hinzugefügt`);
}

export function clearAllCards() {
    updateState(state => {
        state.boxes.forEach(b => {
            b.zeile1 = '';
            b.zeile2 = '';
            b.zeile3 = '';
        });
    }, 'clear_all');

    const dashboard = document.getElementById('dashboard');
    if (dashboard) {
        dashboard.querySelectorAll('.input-z1, .input-z2, .input-z3').forEach(input => {
            input.value = '';
        });
    }

    pushStateToOBS();
    showToast('info', 'Alle Textzeilen geleert');
}
