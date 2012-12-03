// main.cpp
//
// Starter code for CS 148 Assignment 5.
//

//
// Include libst headers for using OpenGL and GLUT.
//
#include "st.h"
#include "stgl.h"
#include "stglut.h"
#include "STHDRImage.h"
#include "STImage.h"

#define WIN_WIDTH  512
#define WIN_HEIGHT 512

#include <stdlib.h>
#include <math.h>
#include "response.h"

enum MODE {
  MODE_RESPONSE,
  MODE_CREATE,
  MODE_VIEW,
  MODE_VP,
  MODE_TONEMAP
};

// delta used to make sure we don't do log(0)
#define LOG_DELTA .000001f

//
// Globals used by this application.
// As a rule, globals are Evil, but this is a small application
// and the design of GLUT makes it hard to avoid them.
//

// Current size of the OpenGL window
int gWindowSizeX;
int gWindowSizeY;

static STImage * image = NULL;

static CameraResponse cr;    // Camera response, either generated or loaded
static STHDRImage * hdr;     // The high dynamic range image

static int mode;             // The type of operation we're doing

static float shutter_time = .125f; // Shutter time for virtual photograph
static float key_lum      = .45f;  // The key value for tonemapping
static float hdr_view_max = 1.f;   // Value to map to max color

/* Recover an HDR image from a set of photos and the response curve for the camera used to
 * take the photos. To work efficiently you should only have one image open at a time.
 * For each pixel, you want to calculate the following:
 *
 * Ls(x,y) = sum_i( weight( pixel(x,y,i) ) * ( Exposure( pixel(x,y,i) ) - ln(dt_i) ) );
 * ws(x,y) = sum_i( weight( pixel(x,y,i) ) );
 * L_w(x,y) = exp( Ls(x,y) / ws(x,y) ).
 *
 * HINT: Look at the function definitions and the comments in response.h!
 */
STHDRImage* recover_hdr(vector<Photo>& photos, CameraResponse& response) {

  /* CS148 TODO */
    STImage sample(photos[0].filename.c_str());
    static const int w = sample.GetWidth();
    static const int h = sample.GetHeight();
    
    STHDRImage *result = new STHDRImage(w, h);
    STHDRImage *Ls = new STHDRImage(w, h);
    STHDRImage *ws = new STHDRImage(w, h);
    
    for(int j = 0; j < photos.size(); j++) {
        STImage curimage(photos[j].filename.c_str());
        float ln_shutter = log(photos[j].shutter);
        STColor3f ln_shutter3f = STColor3f(ln_shutter, ln_shutter, ln_shutter);
        for(int x = 0; x < curimage.GetWidth(); x++) {
            for(int y = 0; y < curimage.GetHeight(); y++) {
                STColor4ub curpixel = curimage.GetPixel(x,y);
                STColor3f exposure = response.GetExposure(curpixel);
                STColor3f weight = response.Weight(curpixel);
                
                STColor3f num = weight * (exposure - ln_shutter3f);
                STColor3f denom = weight;
                Ls->SetPixel(x, y, Ls->GetPixel(x, y) + num);
                ws->SetPixel(x, y, ws->GetPixel(x, y) + denom);
            }
        }
    }
    
    for(int x=0; x < result->GetWidth(); x++) {
        for(int y=0; y < result->GetHeight(); y++) {
            STColor3f Lsp = Ls->GetPixel(x,y);
            STColor3f wsp = ws->GetPixel(x,y);
            
            STColor3f hdp = STColor3f(exp(Lsp.r / wsp.r), exp(Lsp.g / wsp.g), exp(Lsp.b / wsp.b));
            result->SetPixel(x, y, hdp);
            
        }
    }
    return result;
}

/* Scale an HDR image for viewing, this is just a linear map
 * of hdr such that 0 maps to 0 and max_val maps to 255.
 */
void scale_hdr(STHDRImage* hdr, float max_val, STImage* result) {
    for (int x = 0; x < result->GetWidth(); x++) {
        for(int y = 0; y < result->GetHeight(); y++){ 
            STColor3f hdp = hdr->GetPixel(x,y);
            if (hdp.r > max_val) hdp.r = max_val;
            if (hdp.g > max_val) hdp.g = max_val;
            if (hdp.b > max_val) hdp.b = max_val;
            hdp = hdp * (255. / max_val);
            STColor4ub px = STColor4ub(hdp.r, hdp.g, hdp.b, 1.);
            result->SetPixel(x,y,px);
        }
    }
}

/* Take a virtual photo - Use the camera response curve to take a virtual photo using
 * the given shutter time.  Use hdr as the source image and store the virtual photo
 * in result.  Remember that a photo is just the response of the camera to the incoming
 * radiance over a given shutter time.
 */
void virtual_photo(STHDRImage* hdr, CameraResponse& response, float shutter, STImage* result) {

    for (int x = 0; x < result->GetWidth(); x++) {
        for(int y = 0; y < result->GetHeight(); y++){ 
            STColor3f hdp = hdr->GetPixel(x,y);
            STColor4ub px = response.GetResponse(hdp, shutter);
            result->SetPixel(x,y,px);
        }
    }
}

/* Tonemap operator, tonemaps the hdr image and stores the
 * tonemapped image in result.  Use the value key as the 
 * key of the photograph.  This function should:
 * 1) Calculate the log-average luminance, Lavg
 * 2) Calculate the tonemapped image using the equation
 *    L(x,y) = L_w(x,y) * key / Lavg;
 *    C_tm(x,y) = L(x,y) / (1 + L(x,y));
 * 3) Store those tonemapped values to the result image,
 *    scaling appropriately.
 */
void tonemap(STHDRImage* hdr, float key, STImage* result) {
    int numPixels = result->GetWidth() * result->GetHeight();
    float lsum = 0.;
    float sigma = 0.1;
    
    for (int x = 0; x < result->GetWidth(); x++) {
        for(int y = 0; y < result->GetHeight(); y++){ 
            STColor3f hdp = hdr->GetPixel(x,y);
            float luminance = 0.27 * hdp.r + 0.67 * hdp.g + 0.06 * hdp.b;
            lsum += log(sigma + luminance);
        }
    }
    
    float L_w = exp(lsum / numPixels);
    
    for (int x = 0; x < result->GetWidth(); x++) {
        for(int y = 0; y < result->GetHeight(); y++){ 
            STColor3f hdp_scaled = hdr->GetPixel(x,y) * (key / L_w);
            STColor3f C_tm = hdp_scaled / (STColor3f(1.,1.,1.) + hdp_scaled);
            
            STColor4ub px = STColor4ub(C_tm.r * 255, C_tm.g * 255, C_tm.b * 255, 1.);
            result->SetPixel(x,y,px);
        }
    }
}

//
// GLUT display callback
//
void display()
{
  glClearColor(.3f, .3f, .3f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  image->Draw();

  glutSwapBuffers();
}

//
// Reshape window
//
void reshape(int w, int h) {
  gWindowSizeX = w;
  gWindowSizeY = h;

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  gluOrtho2D(0., 1., 0., 1.);
  glViewport(0, 0, w, h);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glutPostRedisplay();
}

// Keyboard event handler:
static void keyboard(unsigned char key, int x, int y) {
  switch (key) {
    // quit
    case '+':
    case '=':
      if (mode == MODE_VIEW) {
        hdr_view_max /= 2.f;
        scale_hdr(hdr, hdr_view_max, image);
      }
      else if (mode == MODE_VP) {
        shutter_time *= powf(2.f, 1.f/3.f); // 1/3 stop
        virtual_photo(hdr, cr, shutter_time, image);
      }
      else if (mode == MODE_TONEMAP) {
        key_lum += .05f;
        tonemap(hdr, key_lum, image);
      }
      break;
    case '-':
    case '_':
      if (mode == MODE_VIEW) {
        hdr_view_max *= 2.f;
        scale_hdr(hdr, hdr_view_max, image);
      }
      else if (mode == MODE_VP) {
        shutter_time /= powf(2.f, 1.f/3.f); // 1/3 stop
        virtual_photo(hdr, cr, shutter_time, image);
      }
      else if (mode == MODE_TONEMAP) {
        key_lum -= .05f;
        tonemap(hdr, key_lum, image);
      }

      break;
    case 's':
    case 'S':
      if (mode == MODE_VIEW)
        image->Save("view.jpg");
      else if (mode == MODE_VP)
        image->Save("vp.jpg");
      else if (mode == MODE_TONEMAP)
        image->Save("tonemap.jpg");
      break;
    case char(27):
    case 'q':
    case 'Q':
      exit(0);
  }

  glutPostRedisplay();
}

static void usage() {
  printf("Usage:\n");
  printf("hdr -response photos.list response_out.cr\n");
  printf("hdr -create photos.list response.cr photo_out.pfm\n");
  printf("hdr -view photo.pfm\n");
  printf("hdr -vp photo.pfm response.cr\n");
  printf("hdr -tonemap photo.pfm\n");
  exit(-1);
}

int main(int argc, char** argv)
{
  //
  // Initialize GLUT.
  //
  glutInit(&argc, argv);
  glutInitDisplayMode( GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
  glutInitWindowPosition(20, 20);
  glutInitWindowSize(
      WIN_WIDTH, WIN_HEIGHT);
  glutCreateWindow("CS148 Assignment 5");


  if (argc < 2) {
    usage();
  }

  if ( strcmp(argv[1], "-response") == 0) {
    if (argc != 4)
      usage();
    mode = MODE_RESPONSE;

    // Defaults
    float lambda = 50.f;
    int nsamples = 100;

    vector<Photo> photos;
    LoadHDRStack(argv[2], photos);
    cr.SolveForResponse(photos, lambda, nsamples);

    cr.Save(argv[3]);
    exit(0);
  }
  else if ( strcmp(argv[1], "-create") == 0) {
    if (argc != 5)
      usage();
    mode = MODE_CREATE;
    vector<Photo> photos;
    LoadHDRStack(argv[2], photos);
    cr.Load(argv[3]);
    hdr = recover_hdr(photos, cr);
    hdr->Save(argv[4]);
    exit(0);
  }
  else {
    hdr = new STHDRImage(argv[2]);
    if(hdr)
      image = new STImage(hdr->GetWidth(), hdr->GetHeight());
    else
      usage();

    if ( strcmp(argv[1], "-view") == 0) {
      if (argc != 3)
        usage();
      mode = MODE_VIEW;
      scale_hdr(hdr, hdr_view_max, image);
    }
    else if ( strcmp(argv[1], "-vp") == 0) {
      if (argc != 4)
        usage();
      mode = MODE_VP;
      cr.Load(argv[3]);
      virtual_photo(hdr, cr, shutter_time, image);
    }
    else if ( strcmp(argv[1], "-tonemap") == 0) {
      if (argc != 3)
        usage();
      mode = MODE_TONEMAP;
      tonemap(hdr, key_lum, image);
    }
    else {
      usage();
    }
  }

  glutReshapeWindow(image->GetWidth(), image->GetHeight());

  glutDisplayFunc(display);
  glutKeyboardFunc(keyboard);
  glutReshapeFunc(reshape);
  glutMainLoop();

  if(image) {
    delete image;
  }

  return 0;
}
