// Real public usage requirements must survive the intermediate import directory.
#if !defined(FASTDDS_DYN_LINK) || !defined(FASTCDR_DYN_LINK) || !defined(FOONATHAN_MEMORY)
#error Preloaded CONFIG dependencies lost their public compile definitions.
#endif
#if defined(FASTDDS_STATIC_LINK) || defined(FASTCDR_STATIC_LINK)
#error Preloaded CONFIG dependencies must remain shared.
#endif
