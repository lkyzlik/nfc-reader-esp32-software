set(COMPONENT_REQUIRES )
set(COMPONENT_PRIV_REQUIRES )

idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  EMBED_TXTFILES rkey_seed.txt
)
