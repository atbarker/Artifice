
#include <linux/rslib.h>

static struct rs_control *rs_decoder;

struct encoded_block{

};

void initialize_rs(void);

int encode(uint32_t data_length, uint8_t *data, void *entropy, struct encoded_block *blocks);

int decode(uint32_t data_length, uint8_t *data, void *entropy, struct encoded_block *blocks);

void cleanup_rs(void);
