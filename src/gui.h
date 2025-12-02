/* Needed by gui.cpp */
void gotorc(int row,int col);
void my_refresh(class imager *im,struct affcallback_info *acbi);
void make_commas(int64 val,char buf[64]);


void gui_startup();
void gui_shutdown();
extern int repaint_screen;
