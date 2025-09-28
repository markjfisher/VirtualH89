#if defined(__GUIglut__) || !defined(__GUIwx__)
///
/// \name GUIglut.h
///
/// A GUI implementation based on glut.
///
/// \date Apr 20, 2017
/// \author David Troendle and Mark Garlanger
///

#include <cassert>
#include "GUIglut.h"
#include <cstring>  // for memset
#include <cmath>    // for ceil
#include <memory>   // for unique_ptr

// High-resolution font rendering using OpenGL textures
// Provides smooth scaling at any window size or scale factor
const int FONT_SCALE = 4;                       // Font resolution multiplier for smooth scaling (reduced from 16x for performance)
const int VIRTUAL_SCALE = 4;                    // Virtual canvas scale factor
const int VIRTUAL_WIDTH = 680 * VIRTUAL_SCALE;  // 2720 pixels wide
const int VIRTUAL_HEIGHT = 540 * VIRTUAL_SCALE; // 2160 pixels tall

// Aspect ratio preservation
bool preserveAspectRatio = true;                     // Default: maintain aspect ratio
const double NATURAL_ASPECT_RATIO = 680.0 / 540.0;  // Original H89 screen ratio (~1.259)

// Color constants for maintainability
const GLubyte FONT_COLOR_R = 255;  // White/amber red component
const GLubyte FONT_COLOR_G = 255;  // White/amber green component  
const GLubyte FONT_COLOR_B = 0;    // Amber blue component (0 for amber)
const GLubyte FONT_COLOR_A = 255;  // Opaque alpha

// OpenGL texture objects for font rendering
GLuint fontTextures[256];  // One texture per character

// How HIGH-RESOLUTION TEXTURE scaling works:
// 1. Generate fonts at 4x resolution (32x80 pixels) as OpenGL textures  
// 2. Render to 2720x2160 virtual canvas using textured quads
// 3. OpenGL viewport scales virtual canvas to actual window size
// 4. Nearest neighbor filtering for retro look and performance
// ==========================================

tKeyboardFunc GUIglut::GUIKeyboardFunc = nullptr;
tDisplayFunc  GUIglut::GUIDisplayFunc;
tDisplayFunc  GUIglut::GUITimerFunc;

unsigned int  GUIglut::m_ms;

// GLUT routine used to redisplay the screen when needed.

#define max(a, b)    ((a) > (b) ? (a) : (b))
#define min(a, b)    ((a) < (b) ? (a) : (b))

GUIglut::GUIglut() : scaledFontTable(nullptr), scaledFontTableSize(0)
{
    // Set inverted character generator for GLUT.
    fontTable = (unsigned char*) fontTableInverted;

    return;
}

GUIglut::~GUIglut()
{
    // Clean up dynamically allocated font table
    delete[] scaledFontTable;
    scaledFontTable = nullptr;
    scaledFontTableSize = 0;
}

void
GUIglut::GUIDisplay(void)
{
    // Render characters using OpenGL textured quads for smooth scaling
    GLfloat color[3] = {1.0, 1.0, 0.0}; // amber
    // GLfloat color[3] = {0.0, 1.0, 0.0}; // green
    // GLfloat color[3] = { 1.0, 1.0, 1.0 };  // white
    // GLfloat color[3] = { 0.5, 0.0, 1.0 };  // purple
    // GLfloat color[3] = { 0.0, 0.8, 0.0 };
    // GLfloat color[3] = { 0.9, 0.9, 0.0 };  // amber
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3fv(color);

    // Set up VIRTUAL CANVAS coordinates (high-resolution space)
    const int VIRTUAL_BORDER = 20 * VIRTUAL_SCALE;  // 80 pixels in virtual space
    const int VIRTUAL_CHAR_HEIGHT = 20 * VIRTUAL_SCALE;  // 80 pixels in virtual space

    // Render text using textured quads (replaces glCallLists)
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Render each character as a textured quad with batching for performance
    // Note: screen_m is column-major: screen_m[col][row], not [row][col]
    GLuint lastTexture = 0;
    for (int row = 0; row < 25; row++) {
        for (int col = 0; col < H19::GetH19()->cols_c; col++) {
            // Access screen data in column-major order: [col][row]
            GLuint charIndex = H19::GetH19()->screen_m[col][row];

            // Skip empty characters or out of range
            if (charIndex == 0 || charIndex >= 256) continue;

            // Calculate position in virtual coordinate space
            float x = VIRTUAL_BORDER + col * 8 * VIRTUAL_SCALE;
            float y = VIRTUAL_BORDER + (24 - row) * 20 * VIRTUAL_SCALE;
            float w = 8 * VIRTUAL_SCALE;  // Character width in virtual space
            float h = 20 * VIRTUAL_SCALE; // Character height in virtual space

            // Only bind texture when it changes (reduces state changes)
            if (fontTextures[charIndex] != lastTexture) {
                glBindTexture(GL_TEXTURE_2D, fontTextures[charIndex]);
                lastTexture = fontTextures[charIndex];
            }
            
            glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(x,     y);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(x + w, y);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(x + w, y + h);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(x,     y + h);
            glEnd();
        }
    }

    glDisable(GL_TEXTURE_2D);

    // Render cursor using textured quad (replaces glCallLists)
    if ((!H19::GetH19()->cursorOff_m) && (H19::GetH19()->curCursor_m))
    {
        GLuint cursor = (H19::GetH19()->cursorBlock_m) ? (128 + 32) : 27;

        // Skip if cursor character is out of range
        if (cursor < 256) {
            // Calculate cursor position in virtual coordinate space
            float x = VIRTUAL_BORDER + min(H19::GetH19()->posX_m, 79) * 8 * VIRTUAL_SCALE;
            float y = VIRTUAL_BORDER + (24 - H19::GetH19()->posY_m) * 20 * VIRTUAL_SCALE;
            float w = 8 * VIRTUAL_SCALE;  // Cursor width in virtual space
            float h = 20 * VIRTUAL_SCALE; // Cursor height in virtual space

            // Enable texture and blend for cursor
            glEnable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            // Render cursor as textured quad (only bind if different from last texture)
            if (fontTextures[cursor] != lastTexture) {
                glBindTexture(GL_TEXTURE_2D, fontTextures[cursor]);
            }
            glBegin(GL_QUADS);
                glTexCoord2f(0.0f, 0.0f); glVertex2f(x,     y);
                glTexCoord2f(1.0f, 0.0f); glVertex2f(x + w, y);
                glTexCoord2f(1.0f, 1.0f); glVertex2f(x + w, y + h);
                glTexCoord2f(0.0f, 1.0f); glVertex2f(x,     y + h);
            glEnd();

            glDisable(GL_TEXTURE_2D);
        }
    }

    glLogicOp(GL_COPY);

    glutSwapBuffers();

    return;
}

void
GUIglut::InitGUI(void)
{
    assert(H19::GetH19());

    int   dummy_argc = 1;
    char* dummy_argv = (char*) "dummy";

    glutInit(&dummy_argc, &dummy_argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(1020, 810);  // Deploy window at comfortable size
    glutInitWindowPosition(200, 50);
    glutCreateWindow((char*) "Virtual Heathkit H-89 All-in-One Computer");

    glClearColor(0.0f, 0.0f, 0.0f, 0.9f);
    // glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glBlendFunc(GL_ONE, GL_ONE);
    // glBlendEquation(GL_FUNC_ADD);
    glBlendColor(0.5, 0.5, 0.5, 0.9);
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);

    glShadeModel(GL_FLAT);

    // Convert 1-bit bitmap to RGBA texture data
    auto convertBitmapToTexture = [](unsigned char* bitmapData, GLubyte* textureData, int width, int height)
    {
        int bytesPerRow = (width + 7) / 8;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int byteIndex = y * bytesPerRow + (x / 8);
                int bitIndex = 7 - (x % 8);
                bool pixelOn = (bitmapData[byteIndex] & (1 << bitIndex)) != 0;

                int pixelIndex = (y * width + x) * 4;  // RGBA = 4 bytes per pixel
                if (pixelOn) {
                    textureData[pixelIndex + 0] = FONT_COLOR_R;  // R: configurable
                    textureData[pixelIndex + 1] = FONT_COLOR_G;  // G: configurable
                    textureData[pixelIndex + 2] = FONT_COLOR_B;  // B: configurable
                    textureData[pixelIndex + 3] = FONT_COLOR_A;  // A: opaque
                } else {
                    textureData[pixelIndex + 0] = 0;    // R: transparent
                    textureData[pixelIndex + 1] = 0;    // G: transparent
                    textureData[pixelIndex + 2] = 0;    // B: transparent
                    textureData[pixelIndex + 3] = 0;    // A: transparent
                }
            }
        }
    };

    GLuint i;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Generate high-resolution font textures for smooth scaling
    const int SCALED_FONT_WIDTH = 8 * FONT_SCALE;   // 128 pixels wide  
    const int SCALED_FONT_HEIGHT = 20 * FONT_SCALE; // 320 pixels tall
    const int BYTES_PER_ROW = (SCALED_FONT_WIDTH + 7) / 8;  // Round up to nearest byte
    const int BYTES_PER_CHAR = BYTES_PER_ROW * SCALED_FONT_HEIGHT;

    // Create scaled font bitmap table (using class member for proper RAII)
    int newTableSize = 0x100 * BYTES_PER_CHAR;
    if (scaledFontTable == nullptr || scaledFontTableSize != newTableSize) {
        delete[] scaledFontTable;  // Safe to delete nullptr
        scaledFontTable = new unsigned char[newTableSize];
        scaledFontTableSize = newTableSize;
    }
    memset(scaledFontTable, 0, scaledFontTableSize);

    // Variable scaling: each 8x20 char becomes SCALED_FONT_WIDTH x SCALED_FONT_HEIGHT
    for (i = 0; i < 0x100; i++)
    {
        for (int srcRow = 0; srcRow < 20; srcRow++)
        {
            unsigned char originalByte = fontTable[i * 20 + srcRow];

            // Scale each font pixel to create high-resolution bitmap  
            for (int destRowOffset = 0; destRowOffset < FONT_SCALE; destRowOffset++)
            {
                int destRow = srcRow * FONT_SCALE + destRowOffset;

                for (int srcBit = 0; srcBit < 8; srcBit++)
                {
                    if (originalByte & (0x80 >> srcBit))
                    {
                        for (int destBitOffset = 0; destBitOffset < FONT_SCALE; destBitOffset++)
                        {
                            int destBit = srcBit * FONT_SCALE + destBitOffset;

                            // Set bit in scaled font block
                            int byteIndex = i * BYTES_PER_CHAR + destRow * BYTES_PER_ROW + (destBit / 8);
                            int bitIndex = 7 - (destBit % 8);
                            scaledFontTable[byteIndex] |= (1 << bitIndex);
                        }
                    }
                }
            }
        }
    }

    // Generate OpenGL textures for each character (replaces display lists)
    glGenTextures(256, fontTextures);

    for (i = 0; i < 0x100; i++)
    {
        // Convert bitmap to RGBA texture data using RAII for exception safety
        std::unique_ptr<GLubyte[]> textureData(new GLubyte[SCALED_FONT_WIDTH * SCALED_FONT_HEIGHT * 4]);
        convertBitmapToTexture(&scaledFontTable[i * BYTES_PER_CHAR], textureData.get(),
                               SCALED_FONT_WIDTH, SCALED_FONT_HEIGHT);

        // Create OpenGL texture
        glBindTexture(GL_TEXTURE_2D, fontTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCALED_FONT_WIDTH, SCALED_FONT_HEIGHT, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, textureData.get());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

        // textureData automatically cleaned up by unique_ptr
    }

    // Note: Special character (wrap around) will be handled during rendering

    // Fonts are rendered as textured quads for maximum compatibility

    glutReshapeFunc(reshape);
    glutSpecialFunc(special);

    glutIgnoreKeyRepeat(1);

    return;
}

void
GUIglut::StartGUI(void)
{
    glutMainLoop();

    return;
}

void
GUIglut::SetTimerFunc(unsigned int ms, tTimerFunc TimerFunc)
{
    assert(TimerFunc);

    m_ms         = ms;
    GUITimerFunc = TimerFunc;

    glutTimerFunc(ms, GLUTTimerFunc, 1);

    return;

}

void
GUIglut::GLUTTimerFunc(int i)
{
    assert(GUITimerFunc);
    GUITimerFunc();


    // Tell glut to redisplay the scene:
    if (H19::GetH19()->checkUpdated())
    {
        glutPostRedisplay();
    }

    glutTimerFunc(m_ms, GLUTTimerFunc, i);

    return;
}

void
GUIglut::SetDisplayFunc(tDisplayFunc DisplayFunc)
{
    assert(DisplayFunc);

    GUIDisplayFunc = DisplayFunc;
    glutDisplayFunc(GLUTDisplayFunc);

    return;

}

void
GUIglut::GLUTDisplayFunc(void)
{
    assert(GUIDisplayFunc);
    GUIDisplayFunc();

    return;
}

void
GUIglut::SetKeyboardFunc(tKeyboardFunc KeyboardFunc)
{
    assert(KeyboardFunc);

    GUIKeyboardFunc = KeyboardFunc;
    glutKeyboardFunc(GLUTKeyboardFunc);

    return;

}

void
GUIglut::GLUTKeyboardFunc(unsigned char Key, int x, int y)
{
    assert(GUIKeyboardFunc);
    GUIKeyboardFunc(Key);

    return;
}


void
GUIglut::reshape(int w,
                 int h)
{
    if (preserveAspectRatio) {
        // Calculate viewport dimensions that preserve the natural aspect ratio
        double windowAspect = (double)w / (double)h;
        int viewportWidth, viewportHeight;
        int viewportX = 0, viewportY = 0;
        
        if (windowAspect > NATURAL_ASPECT_RATIO) {
            // Window is wider than natural ratio - fit to height with pillarboxing
            viewportHeight = h;
            viewportWidth = (int)(h * NATURAL_ASPECT_RATIO);
            viewportX = (w - viewportWidth) / 2;  // Center horizontally
            viewportY = 0;
        } else {
            // Window is taller than natural ratio - fit to width with letterboxing
            viewportWidth = w;
            viewportHeight = (int)(w / NATURAL_ASPECT_RATIO);
            viewportX = 0;
            viewportY = (h - viewportHeight) / 2;  // Center vertically
        }
        
        // Set viewport to maintain aspect ratio with black borders if needed
        glViewport(viewportX, viewportY, viewportWidth, viewportHeight);
    } else {
        // Original behavior: stretch to fill entire window
        glViewport(0, 0, (GLsizei) w, (GLsizei) h);
    }

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // Set up coordinates for VIRTUAL high-resolution canvas
    // This creates the virtual 2720×2160 coordinate space that gets scaled to fit the viewport
    glOrtho(0.0, VIRTUAL_WIDTH, 0.0, VIRTUAL_HEIGHT, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.0f, 0.0f, 0.0f, 0.9f);
    glClear(GL_COLOR_BUFFER_BIT);
}


void
GUIglut::special(int key,
                 int x,
                 int y)
{
    assert(GUIKeyboardFunc);

    // NOTE: GLUT has already differentiated exact keystrokes
    // based on modern keyboard standards. Here we just encode
    // the modern key codes into something convenient to use
    // in the H19 class.
    switch (key)
    {
        case GLUT_KEY_F1:
            GUIKeyboardFunc('S' | 0x80);
            break;

        case GLUT_KEY_F2:
            GUIKeyboardFunc('T' | 0x80);
            break;

        case GLUT_KEY_F3:
            GUIKeyboardFunc('U' | 0x80);
            break;

        case GLUT_KEY_F4:
            GUIKeyboardFunc('V' | 0x80);
            break;

        case GLUT_KEY_F5:
            GUIKeyboardFunc('W' | 0x80);
            break;

        case GLUT_KEY_F6:
            GUIKeyboardFunc('P' | 0x80);
            break;

        case GLUT_KEY_F7:
            GUIKeyboardFunc('Q' | 0x80);
            break;

        case GLUT_KEY_F8:
            GUIKeyboardFunc('R' | 0x80);
            break;

        case GLUT_KEY_HOME:
            GUIKeyboardFunc('H' | 0x80);
            break;

        case GLUT_KEY_UP:
            GUIKeyboardFunc('A' | 0x80);
            break;

        case GLUT_KEY_DOWN:
            GUIKeyboardFunc('B' | 0x80);
            break;

        case GLUT_KEY_LEFT:
            GUIKeyboardFunc('D' | 0x80);
            break;

        case GLUT_KEY_RIGHT:
            GUIKeyboardFunc('C' | 0x80);
            break;

        default:
            break;
    }

    return;
}
#endif
