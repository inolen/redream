#ifndef PROFILER_H
#define PROFILER_H

#define PROF_ENTER(name)
#define PROF_LEAVE()

void prof_enter(const char *name);
void prof_leave();

void prof_count(const char *name, int count);

#endif
