/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic startup code for a Juce application.

  ==============================================================================
*/

#include <GL/glew.h>
#include "../JuceLibraryCode/JuceHeader.h"
#include "MainComponent.cpp"  

//==============================================================================
class _3DAudioVisualizersApplication  : public JUCEApplication
{
public:
    //==============================================================================
    _3DAudioVisualizersApplication() {}

    const String getApplicationName() override       { return ProjectInfo::projectName; }
    const String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override       { return true; }

    //==============================================================================
    // Function to create the main content component
    static Component* createMainContentComponent() 
    {
        return new MainContentComponent();
    }

    //==============================================================================
    void initialise(const String& commandLine) override
    {
        // This method is where you should put your application's initialisation code.
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        // Add your application's shutdown code here.
        mainWindow = nullptr; // (deletes our window)
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        quit();
    }

    void anotherInstanceStarted(const String& commandLine) override
    {
        // When another instance of the app is launched while this one is running,
        // this method is invoked, and the commandLine parameter tells you what
        // the other instance's command-line arguments were.
    }

    //==============================================================================
    /*
        This class implements the desktop window that contains an instance of
        our MainContentComponent class.
    */
    class MainWindow : public DocumentWindow
    {
    public:
        MainWindow(String name) : DocumentWindow(name,
                                                 Desktop::getInstance().getDefaultLookAndFeel().findColour(ResizableWindow::backgroundColourId),
                                                 DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(createMainContentComponent(), true);
            setResizable(true, true);

            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            // This is called when the user tries to close this window. Here, we'll just
            // ask the app to quit when this happens, but you can change this to do
            // whatever you need.
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION(_3DAudioVisualizersApplication)
