// Main.cpp - Cross-platform entry point (macOS uses native menu inside
// MainComponent)

#include "JuceHeader.h"
#include "UI/MainComponent.h"
#include "Utils/Constants.h"
#include "Utils/Localization.h"
#include "Utils/PlatformUtils.h"

#if JUCE_WINDOWS
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

class PitchEditorApplication : public juce::JUCEApplication {
public:
  PitchEditorApplication() {}

  const juce::String getApplicationName() override { return "HachiTune"; }

  const juce::String getApplicationVersion() override { return "1.0.0"; }

  bool moreThanOneInstanceAllowed() override { return true; }

  void initialise(const juce::String &commandLine) override {
    juce::ignoreUnused(commandLine);
    Localization::loadFromSettings();
    mainWindow = std::make_unique<MainWindow>(getApplicationName());
  }

  void shutdown() override { mainWindow = nullptr; }

  void systemRequestedQuit() override { quit(); }

  void anotherInstanceStarted(const juce::String &commandLine) override {
    juce::ignoreUnused(commandLine);
  }

  class MainWindow : public juce::DocumentWindow {
  public:
    MainWindow(juce::String name)
        : DocumentWindow(name, juce::Colour(COLOR_BACKGROUND),
                         DocumentWindow::allButtons,
                         false) // Don't add to desktop yet
    {
      // Ensure window is opaque - this must be set before any
      // transparency-related operations
      setOpaque(true);

      // Set content first, ensuring it's also opaque
      auto *content = new MainComponent();
      content->setOpaque(true);
      setContentOwned(content, true);

      // Now set native title bar after content is set
      setUsingNativeTitleBar(true);

      setResizable(true, true);

      // Ensure window is still opaque before adding to desktop
      // (some operations might affect opacity state)
      setOpaque(true);

      // Now add to desktop after all properties are set
      addToDesktop();

      centreWithSize(getWidth(), getHeight());
      setVisible(true);

#if JUCE_WINDOWS
      // Enable dark mode for title bar
      if (auto *peer = getPeer()) {
        if (auto hwnd = (HWND)peer->getNativeHandle()) {
          // Enable immersive dark mode for title bar
          constexpr DWORD darkMode = 1;
          DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/,
                                &darkMode, sizeof(darkMode));

          // Enable rounded corners on Windows 11+
          DWORD preference = 2; // DWMWCP_ROUND
          DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/,
                                &preference, sizeof(preference));
        }
      }
#elif JUCE_MAC
      // Enable dark mode for macOS window
      if (auto* peer = getPeer())
        PlatformUtils::setDarkAppearance(peer->getNativeHandle());
#endif
    }

    void closeButtonPressed() override {
      JUCEApplication::getInstance()->systemRequestedQuit();
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
  };

private:
  std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(PitchEditorApplication)
