/* prot.h */
void shift_buffer(BYTE * buffer, int length, int n);
BYTE *align_vmax(BYTE * work_buffer, int track_len);
BYTE *align_vmax_cw(BYTE * work_buffer, int track_len);
BYTE *align_vmax3(BYTE * work_buffer, int tracklen);
BYTE *auto_gap(BYTE * work_buffer, int track_len);
BYTE *find_bad_gap(BYTE * work_buffer, int tracklen);
BYTE *find_long_sync(BYTE * work_buffer, int tracklen);
BYTE *auto_gap(BYTE * work_buffer, int tracklen);
void fix_first_gcr(BYTE *gcrdata, int length, int pos);
void fix_last_gcr(BYTE *gcrdata, int length, int pos);

