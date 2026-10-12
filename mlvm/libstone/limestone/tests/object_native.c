#ifdef OBJECT_CALLER
extern int limestone_object_helper(void);
int limestone_object_native(void) { return limestone_object_helper() + 2; }
#else
int limestone_object_helper(void) { return 40; }
#endif
