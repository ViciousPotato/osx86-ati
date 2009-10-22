#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char *GetEDID_DDC1(
    unsigned int *
);

extern int DDC_checksum(
    unsigned char *,
    int
);

#ifdef __cplusplus
}
#endif
