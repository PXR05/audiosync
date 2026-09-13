#ifndef AUDIOSYNC_DETACH_H
#define AUDIOSYNC_DETACH_H
int detach_is_child(void);
void detach_prepare(void);
int detach_run(int argc, char **argv, int immediate);
#endif
