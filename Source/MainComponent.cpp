/**
    Graphics Final Project - Audio Visualizer Suite
    Tim Arterbury
*/
#include <GL/glew.h>    
#include "../JuceLibraryCode/JuceHeader.h"
#include "Oscilloscope2D.h"
#include "Oscilloscope3D.h"
#include "Spectrum.h"
#include "RingBuffer.h"

/** The MainContentComponent is the component that holds all the buttons and
    visualizers. This component fills the entire window.
*/
class MainContentComponent : public AudioAppComponent,
    public ChangeListener,
    public Button::Listener
{
public:
    MainContentComponent() : audioIOSelector(deviceManager, 1, 2, 0, 0, false, false, true, true)
    {
        formatManager.registerBasicFormats();
        audioTransportSource.addChangeListener(this);
        setAudioChannels(2, 2);  // Initially Stereo Input to Stereo Output

        // Initialize ringBuffer before using it
        ringBuffer = new RingBuffer<GLfloat>(2, 1024 * 10); // Adjust buffer size as needed

        // GUI Setup
        addAndMakeVisible(&openFileButton);
        openFileButton.setButtonText("Open File");
        openFileButton.addListener(this);

        addAndMakeVisible(&playButton);
        playButton.setButtonText("Play");
        playButton.addListener(this);
        playButton.setColour(TextButton::buttonColourId, Colours::green);
        playButton.setEnabled(false);  // Enabled only after a file is loaded

        addAndMakeVisible(&stopButton);
        stopButton.setButtonText("Stop");
        stopButton.addListener(this);
        stopButton.setColour(TextButton::buttonColourId, Colours::red);
        stopButton.setEnabled(false);

        // Now that ringBuffer is initialized, we can use it
        spectrum = new Spectrum(ringBuffer);
        addChildComponent(spectrum);
        spectrum->setVisible(true);
        spectrum->start();

        setSize(800, 600); // Set the initial size of the component
    }


    ~MainContentComponent()
    {
        shutdownAudio();
    }

    //==============================================================================
    // Audio Callbacks

    /** Called before rendering Audio.
    */
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override
    {
        // Setup Audio Source
        audioTransportSource.prepareToPlay(samplesPerBlockExpected, sampleRate);

        // Setup Ring Buffer of GLfloat's for the visualizer to use
        ringBuffer = new RingBuffer<GLfloat>(2, samplesPerBlockExpected * 10);

        // Allocate all Visualizers
        oscilloscope2D = new Oscilloscope2D(ringBuffer);
        addChildComponent(oscilloscope2D);

        oscilloscope3D = new Oscilloscope3D(ringBuffer);
        addChildComponent(oscilloscope3D);

        spectrum = new Spectrum(ringBuffer);
        addChildComponent(spectrum);
    }

    /** Called after rendering Audio.
    */
    void releaseResources() override
    {
        // Delete all visualizer allocations
        if (oscilloscope2D != nullptr)
        {
            oscilloscope2D->stop();
            removeChildComponent(oscilloscope2D);
            delete oscilloscope2D;
        }

        if (oscilloscope3D != nullptr)
        {
            oscilloscope3D->stop();
            removeChildComponent(oscilloscope3D);
            delete oscilloscope3D;
        }

        if (spectrum != nullptr)
        {
            spectrum->stop();
            removeChildComponent(spectrum);
            delete spectrum;
        }

        audioTransportSource.releaseResources();
        delete ringBuffer;
    }

    /** The audio rendering callback.
    */
void getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill) override {
    // Clear the buffer first to ensure clean slate for operations
    bufferToFill.clearActiveBufferRegion();

    // Check if file mode is enabled and if the audio source is playing
    if (audioFileModeEnabled && audioTransportSource.isPlaying()) {
        // Get audio data from the file
        audioTransportSource.getNextAudioBlock(bufferToFill);

        // Write the obtained audio samples to the ring buffer for visualization or further processing
        ringBuffer->writeSamples(*bufferToFill.buffer, bufferToFill.startSample, bufferToFill.numSamples);
    }

    // If microphone input is enabled, handle accordingly
    if (audioInputModeEnabled) {
        // Additional handling for mic input can be placed here
        // For now, we're ensuring mic input doesn't produce audible output:
        bufferToFill.clearActiveBufferRegion();
    }

    // If neither mode is enabled, the buffer remains cleared from the initial step
}



    //==============================================================================
    // GUI Callbacks

    /** Paints UI elements and various graphics on the screen. NOT OpenGL.
        This will draw on top of the OpenGL background.
     */
    void paint(Graphics& g) override
    {
        g.fillAll(Colour(0xFF252831)); // Set background color (below any GL Visualizers)
    }

    /** Resizes various JUCE Components (UI elements, etc) placed on the screen. NOT OpenGL.
    */
    void resized() override
    {
        const int buttonHeight = 30;
        const int buttonWidth = getWidth() / 4;
        int margin = 10;

        openFileButton.setBounds(margin, margin, buttonWidth - 2 * margin, buttonHeight);
        playButton.setBounds(openFileButton.getRight() + margin, margin, buttonWidth - 2 * margin, buttonHeight);
        stopButton.setBounds(playButton.getRight() + margin, margin, buttonWidth - 2 * margin, buttonHeight);

        // Set the bounds for the spectrum visualizer
        spectrum->setBounds(0, openFileButton.getBottom() + margin, getWidth(), getHeight() - (openFileButton.getBottom() + margin));
    }


    void changeListenerCallback(ChangeBroadcaster* source) override
    {
        if (source == &audioTransportSource)
        {
            if (audioTransportSource.isPlaying())
                changeAudioTransportState(Playing);
            else if ((audioTransportState == Stopping) || (audioTransportState == Playing))
                changeAudioTransportState(Stopped);
            else if (audioTransportState == Pausing)
                changeAudioTransportState(Paused);
        }
    }
    
void buttonClicked(Button* button) override {
    if (button == &openFileButton) {
        FileChooser chooser("Select a Wave file to play...", File(), "*.wav");
        if (chooser.browseForFileToOpen()) {
            auto file = chooser.getResult();
            auto* reader = formatManager.createReaderFor(file);
            if (reader != nullptr) {
                std::unique_ptr<AudioFormatReaderSource> newSource(new AudioFormatReaderSource(reader, true));
                audioTransportSource.setSource(newSource.get(), 0, nullptr, reader->sampleRate);
                audioReaderSource.reset(newSource.release()); // Ensure the source is kept alive
                playButton.setEnabled(true);
                stopButton.setEnabled(false);
                audioFileModeEnabled = true;
                audioInputModeEnabled = false;
            }
        }
    } else if (button == &playButton && !audioTransportSource.isPlaying()) {
        audioTransportSource.start();
        playButton.setButtonText("Pause");
        stopButton.setEnabled(true);
        audioTransportState = Playing;
    } else if (button == &stopButton) {
        audioTransportSource.stop();
        audioTransportSource.setPosition(0.0);
        playButton.setButtonText("Play");
        playButton.setEnabled(true); // Ensure button is re-enabled
        stopButton.setEnabled(false);
        audioTransportState = Stopped;
    }
}


void handleOpenFileButton() {
    FileChooser chooser("Select a Wave file to play...", File(), "*.wav");
    if (chooser.browseForFileToOpen()) {
        auto file = chooser.getResult();
        DBG("Selected file: " + file.getFullPathName());  // Log file path
        std::unique_ptr<AudioFormatReader> reader(formatManager.createReaderFor(file));

        if (reader) {
            auto* newSource = new AudioFormatReaderSource(reader.release(), true);
            audioTransportSource.setSource(newSource, 0, nullptr, newSource->getAudioFormatReader()->sampleRate);
            audioReaderSource.reset(newSource);
            playButton.setEnabled(true);
            stopButton.setEnabled(false);
            audioFileModeEnabled = true;
            audioInputModeEnabled = false;
            DBG("Audio file loaded successfully");
        } else {
            DBG("Failed to load audio file");
        }
    } else {
        DBG("File chooser was cancelled");
    }
}


void handlePlayPauseButton() {
    if (audioTransportState == Stopped || audioTransportState == Paused) {
        audioTransportSource.start();
        playButton.setButtonText("Pause");
        stopButton.setEnabled(true);
        audioTransportState = Playing;
    } else if (audioTransportState == Playing) {
        audioTransportSource.stop();
        playButton.setButtonText("Play");
        audioTransportState = Paused;
    }
}

void handleStopButton() {
    audioTransportSource.stop();
    audioTransportSource.setPosition(0.0);
    playButton.setButtonText("Play");
    stopButton.setEnabled(false);
    audioTransportState = Stopped;
}
    
    
/*
    void buttonClicked(Button* button) override
    {
        if (button == &openFileButton)
        {
            FileChooser chooser("Select a Wave file to play...", File(), "*.wav");
            if (chooser.browseForFileToOpen())
            {
                File file(chooser.getResult());
                AudioFormatReader* reader = formatManager.createReaderFor(file);

                if (reader != nullptr)
                {
                    audioReaderSource.reset(new AudioFormatReaderSource(reader, true));
                    audioTransportSource.setSource(audioReaderSource.get(), 0, nullptr, reader->sampleRate);
                    playButton.setEnabled(true);
                    stopButton.setEnabled(false);
                    audioFileModeEnabled = true;
                    audioInputModeEnabled = false;
                }
            }
        }
        else if (button == &playButton)
        {
            if ((audioTransportState == Stopped) || (audioTransportState == Paused))
            {
                audioTransportSource.start();
                playButton.setButtonText("Pause");
                stopButton.setEnabled(true);
                audioTransportState = Playing;
            }
            else if (audioTransportState == Playing)
            {
                audioTransportSource.stop();
                playButton.setButtonText("Play");
                audioTransportState = Paused;
            }
        }
        else if (button == &stopButton)
        {
            audioTransportSource.stop();
            audioTransportSource.setPosition(0.0);
            playButton.setButtonText("Play");
            stopButton.setEnabled(false);
            audioTransportState = Stopped;
        }
    }

    void openFileButtonClicked()
    {
        FileChooser chooser("Select a Wave file to play...", File(), "*.wav");
        if (chooser.browseForFileToOpen())
        {
            File file(chooser.getResult());
            AudioFormatReader* reader = formatManager.createReaderFor(file);

            if (reader != nullptr)
            {
                audioReaderSource.reset(new AudioFormatReaderSource(reader, true));
                audioTransportSource.setSource(audioReaderSource.get(), 0, nullptr, reader->sampleRate);
                playButton.setEnabled(true);
                stopButton.setEnabled(false);
                audioFileModeEnabled = true;
                audioInputModeEnabled = false;
            }
        }
    }
*/

private:
    //==============================================================================
    // PRIVATE MEMBERS

    /** Describes one of the states of the audio transport.
    */
    enum AudioTransportState
    {
        Stopped,
        Starting,
        Playing,
        Pausing,
        Paused,
        Stopping
    };

    /** Changes audio transport state.
    */
    void changeAudioTransportState(AudioTransportState newState)
    {
        if (audioTransportState != newState)
        {
            audioTransportState = newState;

            switch (audioTransportState)
            {
            case Stopped:
                playButton.setButtonText("Play");
                stopButton.setButtonText("Stop");
                stopButton.setEnabled(false);
                audioTransportSource.setPosition(0.0);
                break;

            case Starting:
                audioTransportSource.start();
                break;

            case Playing:
                playButton.setButtonText("Pause");
                stopButton.setButtonText("Stop");
                stopButton.setEnabled(true);
                break;

            case Pausing:
                audioTransportSource.stop();
                break;

            case Paused:
                playButton.setButtonText("Resume");
                stopButton.setButtonText("Return to Zero");
                break;

            case Stopping:
                audioTransportSource.stop();
                break;
            }
        }
    }





    /** Triggered when the Mic Input (Audio Input Button) is clicked. It pulls
        audio from the computer's first two audio inputs.
     */
    void audioInputButtonClicked()
    {
        changeAudioTransportState(AudioTransportState::Stopping);
        changeAudioTransportState(AudioTransportState::Stopped);

        audioFileModeEnabled = false;
        audioInputModeEnabled = true;

        playButton.setEnabled(false);
        stopButton.setEnabled(false);
    }

    void showIOSelectorButtonClicked()
    {
        oscilloscope2DButton.setToggleState(false, NotificationType::dontSendNotification);
        oscilloscope3DButton.setToggleState(false, NotificationType::dontSendNotification);
        spectrumButton.setToggleState(false, NotificationType::dontSendNotification);

        bool audioIOShouldBeVisibile = !audioIOSelector.isVisible();

        audioIOSelector.setVisible(audioIOShouldBeVisibile);

        if (audioIOShouldBeVisibile)
        {
            if (oscilloscope2D != nullptr)
            {
                oscilloscope2D->setVisible(false);
                oscilloscope2D->stop();
            }

            if (oscilloscope3D != nullptr)
            {
                oscilloscope3D->setVisible(false);
                oscilloscope3D->stop();
            }

            if (spectrum != nullptr)
            {
                spectrum->setVisible(false);
                spectrum->stop();
            }
        }
        else
        {
            if (oscilloscope2DButton.getToggleState())
            {
                oscilloscope3D->setVisible(true);
                oscilloscope3D->start();
            }
            else if (oscilloscope3DButton.getToggleState())
            {
                oscilloscope3D->setVisible(true);
                oscilloscope3D->start();
            }
            else if (spectrumButton.getToggleState())
            {
                oscilloscope3D->setVisible(true);
                oscilloscope3D->start();
            }
        }
    }


    void playButtonClicked()
    {
        if ((audioTransportState == Stopped) || (audioTransportState == Paused))
            changeAudioTransportState(Starting);
        else if (audioTransportState == Playing)
            changeAudioTransportState(Pausing);
    }

    void stopButtonClicked()
    {
        if (audioTransportState == Paused)
            changeAudioTransportState(Stopped);
        else
            changeAudioTransportState(Stopping);
    }


    //==============================================================================
    // PRIVATE MEMBER VARIABLES

    // App State
    bool audioFileModeEnabled;
    bool audioInputModeEnabled;

    // GUI Buttons
    TextButton openFileButton;
    TextButton audioInputButton;
    TextButton showIOSelectorButton;
    TextButton playButton;
    TextButton stopButton;

    TextButton oscilloscope2DButton;
    TextButton oscilloscope3DButton;
    TextButton spectrumButton;

    AudioDeviceSelectorComponent audioIOSelector;

    // Audio File Reading Variables
    AudioFormatManager formatManager;
    std::unique_ptr<AudioFormatReaderSource> audioReaderSource;
    AudioTransportSource audioTransportSource;
    AudioTransportState audioTransportState;

    // Audio & GL Audio Buffer
    RingBuffer<float>* ringBuffer;

    // Visualizers
    Oscilloscope2D* oscilloscope2D;
    Oscilloscope3D* oscilloscope3D;
    Spectrum* spectrum;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};
