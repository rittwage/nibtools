/* prot.h */
void search_fat_tracks(BYTE *track_buffer, BYTE *track_density, size_t *track_length);
size_t sync_align(BYTE *buffer, int length);
void shift_buffer_left(BYTE * buffer, int length, int n);
void shift_buffer_right(BYTE * buffer, int length, int n);
BYTE *align_vmax(BYTE * work_buffer, size_t track_len);
BYTE *align_vmax_cw(BYTE * work_buffer, size_t track_len);
BYTE *align_vmax_new(BYTE * work_buffer, size_t tracklen);
BYTE *align_pirateslayer(BYTE * work_buffer, size_t tracklen);
BYTE *align_rl_special(BYTE * work_buffer, size_t tracklen);
BYTE *auto_gap(BYTE * work_buffer, size_t track_len);
BYTE *find_bad_gap(BYTE * work_buffer, size_t tracklen);
BYTE *find_long_sync(BYTE * work_buffer, size_t tracklen);
BYTE *auto_gap(BYTE * work_buffer, size_t tracklen);
void fix_first_gcr(BYTE *gcrdata, size_t length, size_t pos);
void fix_last_gcr(BYTE *gcrdata, size_t length, size_t pos);

