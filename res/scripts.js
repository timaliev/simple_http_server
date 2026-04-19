// scripts.js
// vim:set ft=js
//
// Theme manager that remembers both theme mode (light/dark) AND theme name
//
const ThemeManager = {
  // Available themes
  themes: {
    'contrast': {
      name: 'GitHub High Contrast',
      light: 'contrast-light',
      dark: 'contrast-dark'
    },
    'dracula': {
      name: 'Dracula',
      light: 'dracula-light',
      dark: 'dracula-dark'
    },
    'solarized': {
      name: 'Solarized',
      light: 'solarized-light',
      dark: 'solarized-dark'
    },
    'traefik': {
      name: 'Traefik',
      light: 'traefik-light',
      dark: 'traefik-dark'
    },
  },

  // Default theme
  defaultThemeName: 'traefik',
  defaultMode: 'auto',

  // Storage keys
  THEME_NAME_KEY: 'theme_name',
  THEME_MODE_KEY: 'theme_mode',

  /**
   * Initialize theme on page load
   */
  init() {
    // Load saved theme name and mode, or use defaults
    const savedThemeName = localStorage.getItem(this.THEME_NAME_KEY) || this.defaultThemeName;
    const savedMode = localStorage.getItem(this.THEME_MODE_KEY) || this.defaultMode;

    // Apply the saved theme
    this.setTheme(savedThemeName, savedMode);

    // Setup theme toggle listener if you have a toggle button
    this.setupToggleListener();
  },

  /**
    * Apply a mode (light or dark or system) to the document and save local state
    * @param {string} mode - 'light'/'dark'/'auto'
    */ 
  applyMode(mode) {
    // Update tooltip and butto icon according to current mode
    const modeToggle = document.getElementById('data-mode-toggle');
    if (modeToggle) {
      if (mode === 'auto') {
        modeToggle.title = 'Auto (click for Light)';
        autoIcon.classList.remove('hidden');
        moonIcon.classList.add('hidden');
        sunIcon.classList.add('hidden');
      } else if (mode === 'light') {
        modeToggle.title = 'Light (click for Dark)';
        sunIcon.classList.remove('hidden');
        autoIcon.classList.add('hidden');
        moonIcon.classList.add('hidden');
      } else if (mode === 'dark') {
        modeToggle.title = 'Dark (click for Auto)';
        moonIcon.classList.remove('hidden');
        autoIcon.classList.add('hidden');
        sunIcon.classList.add('hidden');
      } else {
        console.warn(`Mode "${mode}" not applied: mode unknown`);
        return
      }   
    } /*else {
      console.warn(`Mode "${mode}" not applied: 'data-mode-toggle' element id not found`);
    }*/

    localStorage.setItem(this.THEME_MODE_KEY, mode);
    document.documentElement.setAttribute('data-mode', (mode === 'auto') ? this.getSystemMode() : mode);   
  },

  /**
   * Set theme by name and mode
   * @param {string} themeName - 'solarized'/'traefik'/'dracula'/etc.
   * @param {string} mode - 'light'/'dark'/'auto'
   */
  setTheme(themeName, mode = 'auto') {
    // Validate inputs
    if (!this.themes[themeName]) {
      console.warn(`Theme "${themeName}" not found, using default`);
      themeName = this.defaultThemeName;
    }

    if (!['light', 'dark', 'auto'].includes(mode)) {
      console.warn(`Mode "${mode}" invalid, using auto`);
      mode = this.defaultMode;
    }

    // Save to localStorage
    localStorage.setItem(this.THEME_NAME_KEY, themeName);
    localStorage.setItem(this.THEME_MODE_KEY, mode);

    // Update document class or data attribute to show which theme name is active
    document.documentElement.setAttribute('data-theme-name', themeName);

    // Update any UI elements showing current theme and mode
    this.updateThemeDisplay(themeName, mode);
  },

  /**
   * Get current theme name
   * @returns {string}
   */
  getCurrentThemeName() {
    return localStorage.getItem(this.THEME_NAME_KEY) || this.defaultThemeName;
  },

  /**
   * Get current mode
   * @returns @enum ['dark','light','auto']
   */
  getCurrentMode() {
    return localStorage.getItem(this.THEME_MODE_KEY) || this.defaultMode;
  },

  /**
   * Get system preference
   * @returns @enum ['dark','light']
   */
  getSystemMode() {
    return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  },

  /**
   * Toggle between light and dark modes within current theme
   * auto → light → dark → auto
   */
  toggleMode() {
    const currentMode = this.getCurrentMode();

    if (currentMode === 'auto') {
      newMode = 'light';
    } else if (currentMode === 'light') {
      newMode = 'dark';
    } else {
      newMode = 'auto';
    }

    this.applyMode(newMode);
  },

  /**
   * Switch to a different theme (preserves current mode)
   * @param {string} themeName
   */
  switchTheme(themeName) {
    const currentMode = this.getCurrentMode();
    this.setTheme(themeName, currentMode);
  },

  /**
   * Update UI to display current theme
   */
  updateThemeDisplay(themeName, mode) {

    const themeSelect = document.querySelector('[data-theme-select]');
    
    // Update theme selector if exists
    if (themeSelect) {
      themeSelect.querySelector('.custom-select-options').classList.toggle('show');
      
      const trigger = themeSelect.querySelector('.custom-select-trigger');
      const options = themeSelect.querySelectorAll('.custom-select-option');
      const selectedOption = themeSelect.querySelector(
        `.custom-select-option[data-value="${themeName}"]`
      );
      
      if (selectedOption) {
        trigger.textContent = selectedOption.textContent;  // "Traefik"
        trigger.textContent = selectedOption.textContent.replace('✓', '').trim(); // Remove ✓ from trigger text
      }
      // Hide dropdown
      themeSelect.querySelector('.custom-select-options').classList.remove('show');
      // Update checkmarks: add 'checked' to active, remove from others
      options.forEach(option => {
        if (option.dataset.value === themeName) {
          option.classList.add('checked');
        } else {
          option.classList.remove('checked');
        }
      });
    }

    // Update mode and mode toggle button if exists
    this.applyMode(mode);
  },

  /**
   * Setup event listeners for theme and mode controls
   */
  setupToggleListener() {
    // Mode toggle button
    const modeToggle = document.getElementById('data-mode-toggle');
    if (modeToggle) {
      modeToggle.addEventListener('click', () => this.toggleMode());
    }

    // Theme selector
    const themeSelect = document.querySelector('[data-theme-select]');
    if (themeSelect) {
      const trigger = themeSelect.querySelector('.custom-select-trigger');
      const options = themeSelect.querySelectorAll('.custom-select-option');
      // Toggle dropdown
      trigger.addEventListener('click', () => {
        themeSelect.querySelector('.custom-select-options').classList.toggle('show');
      });
      // Close theme selector dropdown when clicking outside
      document.addEventListener('click', (e) => {
        if (!themeSelect.contains(e.target)) {
          themeSelect.querySelector('.custom-select-options').classList.remove('show');
        }
      });
      // Handle option selection
      options.forEach(option => {
        option.addEventListener('click', () => {
          const value = option.dataset.value;
          
          // Change displayed text
          trigger.textContent = option.textContent;

          // Update trigger text immediately
          trigger.textContent = option.textContent.replace('✓', '').trim();          
          
          // Hide dropdown
          themeSelect.querySelector('.custom-select-options').classList.remove('show');
          
          // Trigger your theme change
          this.switchTheme(value);
        });
      });
    }

    // Listen for system dark mode preference
    if (window.matchMedia) {
      const mediaQuery = window.matchMedia('(prefers-color-scheme: dark)');
      mediaQuery.addEventListener('change', (e) => {
        // Only auto-switch if user hasn't explicitly set a mode
        if (this.getCurrentMode() === 'auto') {
          this.applyMode(e.matches ? 'dark' : 'light');
        }
      });
    }
  }
};

// Add class to body when JS loads
document.body.classList.add('js-enabled');

// Initialize on DOM ready
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => ThemeManager.init());
} else {
  ThemeManager.init();
}

// Export for use in other modules
if (typeof module !== 'undefined' && module.exports) {
  module.exports = ThemeManager;
}
