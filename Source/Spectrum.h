//
//  Spectrum.h
//  3DAudioVisualizers
//
//  Created by Tim Arterbury on 5/3/17.
//
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <GL/glew.h>                        // GLEW header
#include "RingBuffer.h"

/** Frequency Spectrum visualizer. Uses basic shaders, and calculates all points
    on the CPU as opposed to the OScilloscope3D which calculates points on the
    GPU.
 */

class Spectrum :    public Component,
                    public OpenGLRenderer,
                    public AsyncUpdater
{
    
public:
    Spectrum (RingBuffer<GLfloat> * ringBuffer)
    :   readBuffer (2, RING_BUFFER_READ_SIZE),
        forwardFFT (fftOrder)
    {
        // Sets the version to 3.2
        openGLContext.setOpenGLVersionRequired (OpenGLContext::OpenGLVersion::openGL3_2);
     
        this->ringBuffer = ringBuffer;
        
        // Set default 3D orientation
        draggableOrientation.reset(Vector3D<float>(0.0, 1.0, 0.0));
        
        // Allocate FFT data
        fftData = new GLfloat [2 * fftSize];
        
        // Attach the OpenGL context but do not start [ see start() ]
        openGLContext.setRenderer(this);
        openGLContext.attachTo(*this);
        
        // Setup GUI Overlay Label: Status of Shaders, compiler errors, etc.
        addAndMakeVisible (statusLabel);
        statusLabel.setJustificationType (Justification::topLeft);
        statusLabel.setFont (Font (14.0f));
    }
    
    ~Spectrum()
    {
        // Turn off OpenGL
        openGLContext.setContinuousRepainting (false);
        openGLContext.detach();
        
        delete [] fftData;
        
        // Detach ringBuffer
        ringBuffer = nullptr;
    }
    
    void handleAsyncUpdate() override
    {
        statusLabel.setText (statusText, dontSendNotification);
    }
    
    //==========================================================================
    // Oscilloscope Control Functions
    
    void start()
    {
        openGLContext.setContinuousRepainting (true);
    }
    
    void stop()
    {
        openGLContext.setContinuousRepainting (false);
    }
    
    
    //==========================================================================
    // OpenGL Callbacks
    
    /** Called before rendering OpenGL, after an OpenGLContext has been associated
        with this OpenGLRenderer (this component is a OpenGLRenderer).
        Sets up GL objects that are needed for rendering.*/
void newOpenGLContextCreated() override
{
    // Initialize GLEW
    glewExperimental = GL_TRUE;  // Ensure modern OpenGL functionality
    GLenum err = glewInit();     // Initialize GLEW

    if (GLEW_OK != err)
    {
        DBG("GLEW Initialization failed: " + String((const char*)glewGetErrorString(err)));
        return;
    }

    DBG("GLEW Initialized successfully");

    // Setup Sizing Variables
    xFreqWidth = 3.0f;
    yAmpHeight = 1.0f;
    zTimeDepth = 3.0f;
    xFreqResolution = 50;
    zTimeResolution = 60;

    numVertices = xFreqResolution * zTimeResolution;

    // Initialize XZ Vertices
    initializeXZVertices();

    // Initialize Y Vertices
    initializeYVertices();

    // Setup Buffer Objects
    glGenBuffers(1, &xzVBO);  // Use GLEW's glGenBuffers
    glBindBuffer(GL_ARRAY_BUFFER, xzVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * numVertices * 2, xzVertices, GL_STATIC_DRAW);

    glGenBuffers(1, &yVBO);   // Use GLEW's glGenBuffers
    glBindBuffer(GL_ARRAY_BUFFER, yVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * numVertices, yVertices, GL_STREAM_DRAW);

    // Check if VAOs are supported and generate/bind if available
    if (GLEW_ARB_vertex_array_object)
    {
        glGenVertexArrays(1, &VAO);   // Use GLEW's glGenVertexArrays
        glBindVertexArray(VAO);       // Use GLEW's glBindVertexArray

        // Bind XZ and Y buffers to VAO
        glBindBuffer(GL_ARRAY_BUFFER, xzVBO);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
        glBindBuffer(GL_ARRAY_BUFFER, yVBO);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(GLfloat), nullptr);

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    }
    else
    {
        DBG("VAO not supported, using manual buffer setup.");

        // Fallback to manually binding VBOs and setting up attributes
        glBindBuffer(GL_ARRAY_BUFFER, xzVBO);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
        glBindBuffer(GL_ARRAY_BUFFER, yVBO);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(GLfloat), nullptr);

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    }

    // Set point size for drawing
    glPointSize(10.0f);

    // Setup Shaders
    createShaders();
}


    
    /** Called when done rendering OpenGL, as an OpenGLContext object is closing.
        Frees any GL objects created during rendering.
     */
    void openGLContextClosing() override
    {
        shader.release();
        uniforms.release();
        
        delete [] xzVertices;
        delete [] yVertices;
    }
    
    
    /** The OpenGL rendering callback.
     */
void renderOpenGL() override
{
    jassert(OpenGLHelpers::isContextActive());

    // Setup the viewport according to the window size and device scaling
    const float renderingScale = (float) openGLContext.getRenderingScale();
    glViewport(0, 0, roundToInt(renderingScale * getWidth()), roundToInt(renderingScale * getHeight()));

    // Clear the background with a predefined color
    OpenGLHelpers::clear(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));

    // Enable blending to smooth out the rendering
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Activate the shader program
    shader->use();

    // Retrieve samples from the ring buffer and clear the FFT data array
    ringBuffer->readSamples(readBuffer, RING_BUFFER_READ_SIZE);
    FloatVectorOperations::clear(fftData, 2 * fftSize);

    // Sum audio samples across channels for FFT processing
    for (int i = 0; i < readBuffer.getNumChannels(); ++i)
    {
        FloatVectorOperations::add(fftData, readBuffer.getReadPointer(i), RING_BUFFER_READ_SIZE);
    }

    // Perform the FFT
    forwardFFT.performFrequencyOnlyForwardTransform(fftData);

    // Compute the maximum FFT level for scaling
    Range<float> maxFFTLevel = FloatVectorOperations::findMinAndMax(fftData, fftSize / 2);

    // Update vertex positions based on FFT results, with special attention to properly clear old data
    for (int z = zTimeResolution - 1; z > 0; --z) {
        for (int x = 0; x < xFreqResolution; ++x) {
            yVertices[z * xFreqResolution + x] = yVertices[(z - 1) * xFreqResolution + x];
        }
    }

    // Populate new data at the front row
    for (int x = 0; x < xFreqResolution; ++x) {
        int fftIndex = jmap(x, 0, xFreqResolution - 1, 0, fftSize / 2 - 1);
        float level = jmap(fftData[fftIndex], 0.0f, maxFFTLevel.getEnd(), 0.0f, yAmpHeight);
        yVertices[x] = level; // Populate the front-most row with new FFT data
    }

    // Update the vertex buffer object with the new vertex data
    glBindBuffer(GL_ARRAY_BUFFER, yVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * numVertices, yVertices, GL_DYNAMIC_DRAW);

    // Set projection and view matrices in the shader
    if (uniforms->projectionMatrix != nullptr)
        uniforms->projectionMatrix->setMatrix4(getProjectionMatrix().mat, 1, GL_FALSE);
    if (uniforms->viewMatrix != nullptr)
    {
        Matrix3D<float> scale;
        scale.mat[0] = 2.0f;
        scale.mat[5] = 2.0f;
        scale.mat[10] = 2.0f;
        Matrix3D<float> finalMatrix = scale * getViewMatrix();
        uniforms->viewMatrix->setMatrix4(finalMatrix.mat, 1, GL_FALSE);
    }

    // Draw points from the VAO
    glBindVertexArray(VAO);
    glDrawArrays(GL_POINTS, 0, numVertices);

    // Clear FFT data after use
    zeromem(fftData, sizeof(GLfloat) * 2 * fftSize);

    // Reset state to ensure no interference with other OpenGL calls
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}




    
    
    //==========================================================================
    // JUCE Callbacks
    
    void paint (Graphics& g) override {}
    
    void resized () override
    {
        draggableOrientation.setViewport (getLocalBounds());
        statusLabel.setBounds (getLocalBounds().reduced (4).removeFromTop (75));
    }
    
    void mouseDown (const MouseEvent& e) override
    {
        draggableOrientation.mouseDown (e.getPosition());
    }
    
    void mouseDrag (const MouseEvent& e) override
    {
        draggableOrientation.mouseDrag (e.getPosition());
    }
    
private:
    
    //==========================================================================
    // Mesh Functions
    
    // Initialize the XZ values of vertices
void initializeXZVertices()
{
    int numFloatsXZ = numVertices * 2;
    xzVertices = new GLfloat[numFloatsXZ];
    
    // Adjust the scale to ensure the vertices spread appropriately across the visual space
    GLfloat xStart = -1.5f; // start more centrally
    GLfloat zStart = -1.5f; // start more centrally
    GLfloat xOffset = 3.0f / (xFreqResolution - 1); // spans from -1.5 to 1.5
    GLfloat zOffset = 3.0f / (zTimeResolution - 1); // spans from -1.5 to 1.5

    // Initialize the vertices for the grid on the XZ plane
    for (int i = 0; i < numFloatsXZ; i += 2)
    {
        int xIndex = (i / 2) % xFreqResolution;
        int zIndex = (i / 2) / xFreqResolution;

        xzVertices[i] = xStart + xIndex * xOffset;
        xzVertices[i + 1] = zStart + zIndex * zOffset;
    }
}

    
    // Initialize the Y valies of vertices
    void initializeYVertices()
    {
        // Set all Y values to 0.0
        yVertices = new GLfloat [numVertices];
        memset(yVertices, 0.0f, sizeof(GLfloat) * xFreqResolution * zTimeResolution);
    }
    
    
    //==========================================================================
    // OpenGL Functions
    
    /** Calculates and returns the Projection Matrix.
     */
    Matrix3D<float> getProjectionMatrix() const
    {
        float w = 1.0f / (0.5f + 0.1f);
        float h = w * getLocalBounds().toFloat().getAspectRatio (false);
        return Matrix3D<float>::fromFrustum (-w, w, -h, h, 4.0f, 30.0f);
    }
    
    /** Calculates and returns the View Matrix.
     */
    Matrix3D<float> getViewMatrix() const
    {
        Matrix3D<float> viewMatrix (Vector3D<float> (0.0f, 0.0f, -10.0f));
        Matrix3D<float> rotationMatrix = draggableOrientation.getRotationMatrix();
        
        return rotationMatrix * viewMatrix;
    }
    
    /** Loads the OpenGL Shaders and sets up the whole ShaderProgram
     */
    void createShaders()
    {
vertexShader =
"#version 330 core\n"
"layout (location = 0) in vec2 xzPos;\n"  // Assume xzPos.x is the angle index, xzPos.y is the baseline radius or additional data
"layout (location = 1) in float yPos;\n"  // Extra data if needed, unused
"uniform mat4 projectionMatrix;\n"
"uniform mat4 viewMatrix;\n"
"uniform float audioData[256];\n"
"void main()\n"
"{\n"
"    float angle = xzPos.x * 2.0 * 3.14159 / 255.0;\n"  // Ensure mapping to [0, 2π]
"    float amplitude = audioData[int(xzPos.x)];\n"  // Amplitude affecting radius
"    float radius = xzPos.y + amplitude;  // xzPos.y can be used as the base radius\n"
"    float x = radius * cos(angle);\n"  // Calculate X using cos
"    float z = radius * sin(angle);\n"  // Calculate Z using sin
"    gl_Position = projectionMatrix * viewMatrix * vec4(x, yPos, z, 1.0);\n"
"}\n";




   
        
        // Base Shader
fragmentShader =
"#version 330 core\n"
"out vec4 color;\n"
"void main()\n"
"{\n"
"    color = vec4(1.0f, 0.0f, 2.0f, 1.0f);\n"  // Adjust RGB values as desired
"}\n";

        

        std::unique_ptr<OpenGLShaderProgram> shaderProgramAttempt = std::make_unique<OpenGLShaderProgram> (openGLContext);
        
        if (shaderProgramAttempt->addVertexShader ((vertexShader))
            && shaderProgramAttempt->addFragmentShader ((fragmentShader))
            && shaderProgramAttempt->link())
        {
            uniforms.release();
            shader = std::move (shaderProgramAttempt);
            uniforms.reset (new Uniforms (openGLContext, *shader));
            
            // statusText = "GLSL: v" + String (OpenGLShaderProgram::getLanguageVersion(), 2);
statusText = "";  // This will make the status text empty
          
        }
        else
        {
            statusText = shaderProgramAttempt->getLastError();
        }
        
        triggerAsyncUpdate();
    }
    
    //==============================================================================
    // This class manages the uniform values that the shaders use.
    struct Uniforms
    {
        Uniforms (OpenGLContext& openGLContext, OpenGLShaderProgram& shaderProgram)
        {
            projectionMatrix.reset (createUniform (openGLContext, shaderProgram, "projectionMatrix"));
            viewMatrix.reset (createUniform (openGLContext, shaderProgram, "viewMatrix"));
        }
        
        std::unique_ptr<OpenGLShaderProgram::Uniform> projectionMatrix, viewMatrix;
        //ScopedPointer<OpenGLShaderProgram::Uniform> lightPosition;
        
    private:
        static OpenGLShaderProgram::Uniform* createUniform (OpenGLContext& openGLContext,
                                                            OpenGLShaderProgram& shaderProgram,
                                                            const char* uniformName)
        {
            if (glGetUniformLocation (shaderProgram.getProgramID(), uniformName) < 0)
                return nullptr;
            
            return new OpenGLShaderProgram::Uniform (shaderProgram, uniformName);
        }
    };

    // Visualizer Variables
    GLfloat xFreqWidth;
    GLfloat yAmpHeight;
    GLfloat zTimeDepth;
    int xFreqResolution;
    int zTimeResolution;
    
    int numVertices;
    GLfloat * xzVertices;
    GLfloat * yVertices;
    
    
    // OpenGL Variables
    OpenGLContext openGLContext;
    GLuint xzVBO;
    GLuint yVBO;
    GLuint VAO;/*, EBO;*/
    
    std::unique_ptr<OpenGLShaderProgram> shader;
    std::unique_ptr<Uniforms> uniforms;
    
    const char* vertexShader;
    const char* fragmentShader;
    
    
    // GUI Interaction
    Draggable3DOrientation draggableOrientation;
    
    // Audio Structures
    RingBuffer<GLfloat> * ringBuffer;
    AudioBuffer<GLfloat> readBuffer;    // Stores data read from ring buffer
    juce::dsp::FFT forwardFFT;
    GLfloat * fftData;
    
    // This is so that we can initialize fowardFFT in the constructor with the order
    enum
    {
        fftOrder = 10,
        fftSize  = 1 << fftOrder // set 10th bit to one
    };
    
    // Overlay GUI
    String statusText;
    Label statusLabel;
    
    /** DEV NOTE
        If I wanted to optionally have an interchangeable shader system,
        this would be fairly easy to add. Chack JUCE Demo -> OpenGLDemo.cpp for
        an implementation example of this. For now, we'll just allow these
        shader files to be static instead of interchangeable and dynamic.
        String newVertexShader, newFragmentShader;
     */
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Spectrum)
};
