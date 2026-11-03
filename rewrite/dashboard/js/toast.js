/**
 * @file toast.js
 * Toast-Benachrichtigungssystem für Statusmeldungen im Dashboard.
 *
 * Gehört zu Version 2 (rewrite/dashboard/).
 * Zusammenspiel:
 *   - Zeigt temporäre Benachrichtigungen (Erfolg, Fehler, Warnung, Info) an.
 *   - Erzeugt den Toast-Container dynamisch, falls im DOM nicht vorhanden.
 *   - Maskiert Textinhalte über escapeHtml (box-utils.js).
 */

import { escapeHtml } from './box-utils.js';

let toastContainer = null;

/**
 * Stellt sicher, dass das Container-Element für Toasts im DOM existiert.
 *
 * @returns {HTMLElement} Der Toast-Container.
 */
function ensureContainer() {
    if (!toastContainer || !document.body.contains(toastContainer)) {
        toastContainer = document.getElementById('toast-container');
        if (!toastContainer) {
            toastContainer = document.createElement('div');
            toastContainer.id = 'toast-container';
            toastContainer.className = 'toast-container';
            toastContainer.setAttribute('aria-live', 'polite');
            toastContainer.setAttribute('aria-atomic', 'true');
            document.body.appendChild(toastContainer);
        }
    }
    return toastContainer;
}

const ICONS = {
    success: '✓',
    error: '✕',
    warning: '⚠',
    info: 'ℹ'
};

/**
 * Zeigt eine flüchtige Toast-Benachrichtigung in der Benutzeroberfläche an.
 *
 * @param {'success'|'error'|'warning'|'info'} [type='info'] - Art der Benachrichtigung.
 * @param {string} [message=''] - Anzuzeigender Text (wird HTML-maskiert).
 * @param {number} [durationMs=2800] - Anzeigedauer in Millisekunden vor dem Ausblenden.
 */
export function showToast(type = 'info', message = '', durationMs = 2800) {
    const container = ensureContainer();
    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    toast.setAttribute('role', type === 'error' ? 'alert' : 'status');

    const icon = ICONS[type] || ICONS.info;
    toast.innerHTML = `<span class="toast-icon" aria-hidden="true">${icon}</span><span class="toast-message">${escapeHtml(message)}</span>`;

    container.appendChild(toast);

    // Eintrittsanimation
    requestAnimationFrame(() => {
        toast.classList.add('toast-visible');
    });

    setTimeout(() => {
        toast.classList.remove('toast-visible');
        toast.classList.add('toast-hiding');
        setTimeout(() => toast.remove(), 260);
    }, durationMs);
}
