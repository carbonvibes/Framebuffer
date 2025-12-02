#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

// Global variables for cleanup
Display *display = NULL;
Window overlay_window = 0;

void cleanup(int sig) {
    printf("Cleaning up blue overlay...\n");
    if (overlay_window && display) {
        XDestroyWindow(display, overlay_window);
    }
    if (display) {
        XCloseDisplay(display);
    }
    printf(" Overlay removed!\n");
    exit(0);
}

int main() {
    Window root;
    XSetWindowAttributes attrs;
    XColor blue_color;
    Colormap colormap;
    int screen;
    unsigned int width, height;
    
    printf("🔵 === FULL SCREEN BLUE OVERLAY TEST === 🔵\n");
    printf("This will make your ENTIRE SCREEN BRIGHT BLUE!\n");
    printf("Press Ctrl+C to remove the overlay\n\n");
    
    // Set up signal handler for cleanup
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    
    // Open display
    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "Error: Cannot open X display\n");
        fprintf(stderr, "Make sure you're running this in a graphical environment (not SSH)\n");
        return 1;
    }
    
    screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    
    // Get screen dimensions
    width = DisplayWidth(display, screen);
    height = DisplayHeight(display, screen);
    
    printf("Screen size: %dx%d\n", width, height);
    printf("Creating bright blue overlay...\n");
    
    // Create colormap and blue color
    colormap = DefaultColormap(display, screen);
    
    // Create BRIGHT BLUE color
    blue_color.red = 0;
    blue_color.green = 0;
    blue_color.blue = 65535;  // Maximum blue intensity
    blue_color.flags = DoRed | DoGreen | DoBlue;
    
    if (XAllocColor(display, colormap, &blue_color) == 0) {
        fprintf(stderr, "❌ Error: Cannot allocate blue color\n");
        XCloseDisplay(display);
        return 1;
    }
    
    // Set window attributes for full screen overlay
    attrs.background_pixel = blue_color.pixel;
    attrs.override_redirect = True;  // Bypass window manager decorations
    attrs.save_under = False;
    attrs.backing_store = NotUseful;
    attrs.event_mask = ExposureMask;
    
    // Create full screen window that covers everything
    overlay_window = XCreateWindow(display, root,
                                  0, 0,                    // Top-left corner
                                  width, height,           // Full screen size
                                  0,                       // No border
                                  CopyFromParent,          // Same depth as parent
                                  InputOutput,             // Normal window
                                  CopyFromParent,          // Same visual as parent
                                  CWBackPixel | CWOverrideRedirect | 
                                  CWSaveUnder | CWBackingStore | CWEventMask,
                                  &attrs);
    
    if (!overlay_window) {
        fprintf(stderr, "❌ Error: Cannot create overlay window\n");
        XCloseDisplay(display);
        return 1;
    }
    
    printf("🚀 Applying blue overlay NOW!\n");
    
    // Make the window visible
    XMapWindow(display, overlay_window);
    
    // Raise to top and grab focus
    XRaiseWindow(display, overlay_window);
    
    // Force immediate display update
    XSync(display, False);
    XFlush(display);
    
    printf("\n🔵🔵🔵 BLUE OVERLAY IS ACTIVE! 🔵🔵🔵\n");
    printf("Your ENTIRE SCREEN should be BRIGHT BLUE now!\n");
    printf("If you can still see this text, the overlay might be behind other windows.\n");
    printf("Press Ctrl+C to remove the overlay and return to normal.\n\n");
    
    // Keep the overlay active and on top
    int counter = 0;
    while (1) {
        XEvent event;
        
        // Process X11 events
        while (XPending(display)) {
            XNextEvent(display, &event);
            
            // Redraw on expose
            if (event.type == Expose) {
                XClearWindow(display, overlay_window);
                XSync(display, False);
            }
        }
        
        // Periodically ensure window stays on top
        if (counter % 10 == 0) {
            XRaiseWindow(display, overlay_window);
            XSync(display, False);
        }
        
        counter++;
        usleep(50000);  // 50ms delay
    }
    
    // This won't be reached normally
    cleanup(0);
    return 0;
}
