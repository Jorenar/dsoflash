#include <fel.h>

#define SDRAM_ADDR          (0x80000000UL)              // SDRAM base address

#define SDRAM_CMDBUF        (SDRAM_ADDR)                // cmd buffer address
#define SDRAM_CMDBUF_SZ     (1024U*1024)                // cmd buffer size (1MB)

#define SDRAM_DATABUF       (SDRAM_ADDR+SDRAM_CMDBUF_SZ)// data buffer address
#define SDRAM_DATABUF_SZ    (63U*1024*1024)             // dat buffer size(63MB)

static uint8_t sdram_initialized;

static int chip_detect(struct xfel_ctx_t * ctx, uint32_t id)
{
	if(id == 0x00166300)
		return 1;
	return 0;
}

static int chip_reset(struct xfel_ctx_t * ctx)
{
	uint32_t val;

	val = R32(0x01c20ca0 + 0x18);
	val &= ~(0xf << 4);
	val |= (1 << 4) | (0x1 << 0);
	W32(0x01c20ca0 + 0x18, val);
	W32(0x01c20ca0 + 0x10, (0xa57 << 1) | (1 << 0));
	return 1;
}

static int chip_sid(struct xfel_ctx_t * ctx, char * sid)
{
	uint32_t swapbuf, swaplen, cmdlen;
	uint8_t tx[5], rx[8];

	/*
	 * The f1c100s have no sid, using spi nor flash's id.
	 */
	if(fel_spi_init(ctx, &swapbuf, &swaplen, &cmdlen))
	{
		tx[0] = 0x4b;
		tx[1] = 0x0;
		tx[2] = 0x0;
		tx[3] = 0x0;
		tx[4] = 0x0;
		if(fel_spi_xfer(ctx, swapbuf, swaplen, cmdlen, tx, 5, rx, 8))
		{
			sprintf(sid, "%02x%02x%02x%02x%02x%02x%02x%02x", rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], rx[6], rx[7]);
			return 1;
		}
	}
	return 0;
}

static int chip_jtag(struct xfel_ctx_t * ctx)
{
	static const uint8_t payload[] = {
		0xff, 0xff, 0xff, 0xea, 0x40, 0x00, 0xa0, 0xe3, 0x00, 0xd0, 0x80, 0xe5,
		0x04, 0xe0, 0x80, 0xe5, 0x00, 0xe0, 0x0f, 0xe1, 0x08, 0xe0, 0x80, 0xe5,
		0x10, 0xef, 0x11, 0xee, 0x0c, 0xe0, 0x80, 0xe5, 0x10, 0xef, 0x11, 0xee,
		0x10, 0xe0, 0x80, 0xe5, 0x1a, 0x00, 0x00, 0xeb, 0x04, 0x00, 0xa0, 0xe3,
		0x65, 0x10, 0xa0, 0xe3, 0x00, 0x10, 0xc0, 0xe5, 0x47, 0x10, 0xa0, 0xe3,
		0x01, 0x10, 0xc0, 0xe5, 0x4f, 0x10, 0xa0, 0xe3, 0x02, 0x10, 0xc0, 0xe5,
		0x4e, 0x10, 0xa0, 0xe3, 0x03, 0x10, 0xc0, 0xe5, 0x2e, 0x10, 0xa0, 0xe3,
		0x04, 0x10, 0xc0, 0xe5, 0x46, 0x10, 0xa0, 0xe3, 0x05, 0x10, 0xc0, 0xe5,
		0x45, 0x10, 0xa0, 0xe3, 0x06, 0x10, 0xc0, 0xe5, 0x4c, 0x10, 0xa0, 0xe3,
		0x07, 0x10, 0xc0, 0xe5, 0x40, 0x00, 0xa0, 0xe3, 0x00, 0xd0, 0x90, 0xe5,
		0x04, 0xe0, 0x90, 0xe5, 0x10, 0x10, 0x90, 0xe5, 0x10, 0x1f, 0x01, 0xee,
		0x0c, 0x10, 0x90, 0xe5, 0x10, 0x1f, 0x01, 0xee, 0x08, 0x10, 0x90, 0xe5,
		0x01, 0xf0, 0x29, 0xe1, 0x1e, 0xff, 0x2f, 0xe1, 0x40, 0x30, 0x9f, 0xe5,
		0xb4, 0x28, 0x93, 0xe5, 0x0f, 0x20, 0xc2, 0xe3, 0x03, 0x20, 0x82, 0xe3,
		0xb4, 0x28, 0x83, 0xe5, 0xb4, 0x28, 0x93, 0xe5, 0xf0, 0x20, 0xc2, 0xe3,
		0x30, 0x20, 0x82, 0xe3, 0xb4, 0x28, 0x83, 0xe5, 0xb4, 0x28, 0x93, 0xe5,
		0x0f, 0x2a, 0xc2, 0xe3, 0x03, 0x2a, 0x82, 0xe3, 0xb4, 0x28, 0x83, 0xe5,
		0xb4, 0x28, 0x93, 0xe5, 0x0f, 0x26, 0xc2, 0xe3, 0x03, 0x26, 0x82, 0xe3,
		0xb4, 0x28, 0x83, 0xe5, 0x1e, 0xff, 0x2f, 0xe1, 0x00, 0x00, 0xc2, 0x01
	};
	fel_write(ctx, 0x00008800, (void *)&payload[0], sizeof(payload));
	fel_exec(ctx, 0x00008800);
	return 1;
}

static int chip_ddr(struct xfel_ctx_t * ctx, const char * type)
{
	static const uint8_t payload[] = {
		0xff, 0xff, 0xff, 0xea, 0x40, 0x00, 0xa0, 0xe3, 0x00, 0xd0, 0x80, 0xe5,
		0x04, 0xe0, 0x80, 0xe5, 0x00, 0xe0, 0x0f, 0xe1, 0x08, 0xe0, 0x80, 0xe5,
		0x10, 0xef, 0x11, 0xee, 0x0c, 0xe0, 0x80, 0xe5, 0x10, 0xef, 0x11, 0xee,
		0x10, 0xe0, 0x80, 0xe5, 0x6a, 0x01, 0x00, 0xeb, 0x96, 0x01, 0x00, 0xeb,
		0xb2, 0x02, 0x00, 0xeb, 0x04, 0x00, 0xa0, 0xe3, 0x65, 0x10, 0xa0, 0xe3,
		0x00, 0x10, 0xc0, 0xe5, 0x47, 0x10, 0xa0, 0xe3, 0x01, 0x10, 0xc0, 0xe5,
		0x4f, 0x10, 0xa0, 0xe3, 0x02, 0x10, 0xc0, 0xe5, 0x4e, 0x10, 0xa0, 0xe3,
		0x03, 0x10, 0xc0, 0xe5, 0x2e, 0x10, 0xa0, 0xe3, 0x04, 0x10, 0xc0, 0xe5,
		0x46, 0x10, 0xa0, 0xe3, 0x05, 0x10, 0xc0, 0xe5, 0x45, 0x10, 0xa0, 0xe3,
		0x06, 0x10, 0xc0, 0xe5, 0x4c, 0x10, 0xa0, 0xe3, 0x07, 0x10, 0xc0, 0xe5,
		0x40, 0x00, 0xa0, 0xe3, 0x00, 0xd0, 0x90, 0xe5, 0x04, 0xe0, 0x90, 0xe5,
		0x10, 0x10, 0x90, 0xe5, 0x10, 0x1f, 0x01, 0xee, 0x0c, 0x10, 0x90, 0xe5,
		0x10, 0x1f, 0x01, 0xee, 0x08, 0x10, 0x90, 0xe5, 0x01, 0xf0, 0x29, 0xe1,
		0x1e, 0xff, 0x2f, 0xe1, 0x00, 0x00, 0x51, 0xe1, 0x91, 0x00, 0x00, 0x3a,
		0x00, 0x00, 0xa0, 0x03, 0x0e, 0xf0, 0xa0, 0x01, 0x01, 0x40, 0x2d, 0xe9,
		0x04, 0x20, 0x52, 0xe2, 0x20, 0x00, 0x00, 0xba, 0x03, 0xc0, 0x10, 0xe2,
		0x28, 0x00, 0x00, 0x1a, 0x03, 0xc0, 0x11, 0xe2, 0x32, 0x00, 0x00, 0x1a,
		0x08, 0x20, 0x52, 0xe2, 0x12, 0x00, 0x00, 0xba, 0x14, 0x20, 0x52, 0xe2,
		0x0b, 0x00, 0x00, 0xba, 0x10, 0x00, 0x2d, 0xe9, 0x18, 0x50, 0xb1, 0xe8,
		0x18, 0x50, 0xa0, 0xe8, 0x18, 0x50, 0xb1, 0xe8, 0x18, 0x50, 0xa0, 0xe8,
		0x20, 0x20, 0x52, 0xe2, 0xf9, 0xff, 0xff, 0xaa, 0x10, 0x00, 0x72, 0xe3,
		0x18, 0x50, 0xb1, 0xa8, 0x18, 0x50, 0xa0, 0xa8, 0x10, 0x20, 0x42, 0xa2,
		0x10, 0x00, 0xbd, 0xe8, 0x14, 0x20, 0x92, 0xe2, 0x08, 0x50, 0xb1, 0xa8,
		0x08, 0x50, 0xa0, 0xa8, 0x0c, 0x20, 0x52, 0xa2, 0xfb, 0xff, 0xff, 0xaa,
		0x08, 0x20, 0x92, 0xe2, 0x05, 0x00, 0x00, 0xba, 0x04, 0x20, 0x52, 0xe2,
		0x04, 0x30, 0x91, 0xb4, 0x04, 0x30, 0x80, 0xb4, 0x08, 0x10, 0xb1, 0xa8,
		0x08, 0x10, 0xa0, 0xa8, 0x04, 0x20, 0x42, 0xa2, 0x04, 0x20, 0x92, 0xe2,
		0x01, 0x80, 0xbd, 0x08, 0x02, 0x00, 0x52, 0xe3, 0x01, 0x30, 0xd1, 0xe4,
		0x01, 0x30, 0xc0, 0xe4, 0x01, 0x30, 0xd1, 0xa4, 0x01, 0x30, 0xc0, 0xa4,
		0x01, 0x30, 0xd1, 0xc4, 0x01, 0x30, 0xc0, 0xc4, 0x01, 0x80, 0xbd, 0xe8,
		0x04, 0xc0, 0x6c, 0xe2, 0x02, 0x00, 0x5c, 0xe3, 0x01, 0x30, 0xd1, 0xe4,
		0x01, 0x30, 0xc0, 0xe4, 0x01, 0x30, 0xd1, 0xa4, 0x01, 0x30, 0xc0, 0xa4,
		0x01, 0x30, 0xd1, 0xc4, 0x01, 0x30, 0xc0, 0xc4, 0x0c, 0x20, 0x52, 0xe0,
		0xeb, 0xff, 0xff, 0xba, 0x03, 0xc0, 0x11, 0xe2, 0xcc, 0xff, 0xff, 0x0a,
		0x03, 0x10, 0xc1, 0xe3, 0x04, 0xe0, 0x91, 0xe4, 0x02, 0x00, 0x5c, 0xe3,
		0x36, 0x00, 0x00, 0xca, 0x1a, 0x00, 0x00, 0x0a, 0x0c, 0x00, 0x52, 0xe3,
		0x10, 0x00, 0x00, 0xba, 0x0c, 0x20, 0x42, 0xe2, 0x30, 0x00, 0x2d, 0xe9,
		0x2e, 0x34, 0xa0, 0xe1, 0x30, 0x50, 0xb1, 0xe8, 0x04, 0x3c, 0x83, 0xe1,
		0x24, 0x44, 0xa0, 0xe1, 0x05, 0x4c, 0x84, 0xe1, 0x25, 0x54, 0xa0, 0xe1,
		0x0c, 0x5c, 0x85, 0xe1, 0x2c, 0xc4, 0xa0, 0xe1, 0x0e, 0xcc, 0x8c, 0xe1,
		0x38, 0x10, 0xa0, 0xe8, 0x10, 0x20, 0x52, 0xe2, 0xf3, 0xff, 0xff, 0xaa,
		0x30, 0x00, 0xbd, 0xe8, 0x0c, 0x20, 0x92, 0xe2, 0x05, 0x00, 0x00, 0xba,
		0x2e, 0xc4, 0xa0, 0xe1, 0x04, 0xe0, 0x91, 0xe4, 0x0e, 0xcc, 0x8c, 0xe1,
		0x04, 0xc0, 0x80, 0xe4, 0x04, 0x20, 0x52, 0xe2, 0xf9, 0xff, 0xff, 0xaa,
		0x03, 0x10, 0x41, 0xe2, 0xc9, 0xff, 0xff, 0xea, 0x0c, 0x00, 0x52, 0xe3,
		0x10, 0x00, 0x00, 0xba, 0x0c, 0x20, 0x42, 0xe2, 0x30, 0x00, 0x2d, 0xe9,
		0x2e, 0x38, 0xa0, 0xe1, 0x30, 0x50, 0xb1, 0xe8, 0x04, 0x38, 0x83, 0xe1,
		0x24, 0x48, 0xa0, 0xe1, 0x05, 0x48, 0x84, 0xe1, 0x25, 0x58, 0xa0, 0xe1,
		0x0c, 0x58, 0x85, 0xe1, 0x2c, 0xc8, 0xa0, 0xe1, 0x0e, 0xc8, 0x8c, 0xe1,
		0x38, 0x10, 0xa0, 0xe8, 0x10, 0x20, 0x52, 0xe2, 0xf3, 0xff, 0xff, 0xaa,
		0x30, 0x00, 0xbd, 0xe8, 0x0c, 0x20, 0x92, 0xe2, 0x05, 0x00, 0x00, 0xba,
		0x2e, 0xc8, 0xa0, 0xe1, 0x04, 0xe0, 0x91, 0xe4, 0x0e, 0xc8, 0x8c, 0xe1,
		0x04, 0xc0, 0x80, 0xe4, 0x04, 0x20, 0x52, 0xe2, 0xf9, 0xff, 0xff, 0xaa,
		0x02, 0x10, 0x41, 0xe2, 0xae, 0xff, 0xff, 0xea, 0x0c, 0x00, 0x52, 0xe3,
		0x10, 0x00, 0x00, 0xba, 0x0c, 0x20, 0x42, 0xe2, 0x30, 0x00, 0x2d, 0xe9,
		0x2e, 0x3c, 0xa0, 0xe1, 0x30, 0x50, 0xb1, 0xe8, 0x04, 0x34, 0x83, 0xe1,
		0x24, 0x4c, 0xa0, 0xe1, 0x05, 0x44, 0x84, 0xe1, 0x25, 0x5c, 0xa0, 0xe1,
		0x0c, 0x54, 0x85, 0xe1, 0x2c, 0xcc, 0xa0, 0xe1, 0x0e, 0xc4, 0x8c, 0xe1,
		0x38, 0x10, 0xa0, 0xe8, 0x10, 0x20, 0x52, 0xe2, 0xf3, 0xff, 0xff, 0xaa,
		0x30, 0x00, 0xbd, 0xe8, 0x0c, 0x20, 0x92, 0xe2, 0x05, 0x00, 0x00, 0xba,
		0x2e, 0xcc, 0xa0, 0xe1, 0x04, 0xe0, 0x91, 0xe4, 0x0e, 0xc4, 0x8c, 0xe1,
		0x04, 0xc0, 0x80, 0xe4, 0x04, 0x20, 0x52, 0xe2, 0xf9, 0xff, 0xff, 0xaa,
		0x01, 0x10, 0x41, 0xe2, 0x93, 0xff, 0xff, 0xea, 0x02, 0x10, 0x81, 0xe0,
		0x02, 0x00, 0x80, 0xe0, 0x04, 0x20, 0x52, 0xe2, 0x1f, 0x00, 0x00, 0xba,
		0x03, 0xc0, 0x10, 0xe2, 0x27, 0x00, 0x00, 0x1a, 0x03, 0xc0, 0x11, 0xe2,
		0x30, 0x00, 0x00, 0x1a, 0x08, 0x20, 0x52, 0xe2, 0x11, 0x00, 0x00, 0xba,
		0x10, 0x40, 0x2d, 0xe9, 0x14, 0x20, 0x52, 0xe2, 0x05, 0x00, 0x00, 0xba,
		0x18, 0x50, 0x31, 0xe9, 0x18, 0x50, 0x20, 0xe9, 0x18, 0x50, 0x31, 0xe9,
		0x18, 0x50, 0x20, 0xe9, 0x20, 0x20, 0x52, 0xe2, 0xf9, 0xff, 0xff, 0xaa,
		0x10, 0x00, 0x72, 0xe3, 0x18, 0x50, 0x31, 0xa9, 0x18, 0x50, 0x20, 0xa9,
		0x10, 0x20, 0x42, 0xa2, 0x14, 0x20, 0x92, 0xe2, 0x08, 0x50, 0x31, 0xa9,
		0x08, 0x50, 0x20, 0xa9, 0x0c, 0x20, 0x42, 0xa2, 0x10, 0x40, 0xbd, 0xe8,
		0x08, 0x20, 0x92, 0xe2, 0x05, 0x00, 0x00, 0xba, 0x04, 0x20, 0x52, 0xe2,
		0x04, 0x30, 0x31, 0xb5, 0x04, 0x30, 0x20, 0xb5, 0x08, 0x10, 0x31, 0xa9,
		0x08, 0x10, 0x20, 0xa9, 0x04, 0x20, 0x42, 0xa2, 0x04, 0x20, 0x92, 0xe2,
		0x0e, 0xf0, 0xa0, 0x01, 0x02, 0x00, 0x52, 0xe3, 0x01, 0x30, 0x71, 0xe5,
		0x01, 0x30, 0x60, 0xe5, 0x01, 0x30, 0x71, 0xa5, 0x01, 0x30, 0x60, 0xa5,
		0x01, 0x30, 0x71, 0xc5, 0x01, 0x30, 0x60, 0xc5, 0x0e, 0xf0, 0xa0, 0xe1,
		0x02, 0x00, 0x5c, 0xe3, 0x01, 0x30, 0x71, 0xe5, 0x01, 0x30, 0x60, 0xe5,
		0x01, 0x30, 0x71, 0xa5, 0x01, 0x30, 0x60, 0xa5, 0x01, 0x30, 0x71, 0xc5,
		0x01, 0x30, 0x60, 0xc5, 0x0c, 0x20, 0x52, 0xe0, 0xec, 0xff, 0xff, 0xba,
		0x03, 0xc0, 0x11, 0xe2, 0xce, 0xff, 0xff, 0x0a, 0x03, 0x10, 0xc1, 0xe3,
		0x00, 0x30, 0x91, 0xe5, 0x02, 0x00, 0x5c, 0xe3, 0x36, 0x00, 0x00, 0xba,
		0x1a, 0x00, 0x00, 0x0a, 0x0c, 0x00, 0x52, 0xe3, 0x10, 0x00, 0x00, 0xba,
		0x0c, 0x20, 0x42, 0xe2, 0x30, 0x40, 0x2d, 0xe9, 0x03, 0xe4, 0xa0, 0xe1,
		0x38, 0x10, 0x31, 0xe9, 0x2c, 0xec, 0x8e, 0xe1, 0x0c, 0xc4, 0xa0, 0xe1,
		0x25, 0xcc, 0x8c, 0xe1, 0x05, 0x54, 0xa0, 0xe1, 0x24, 0x5c, 0x85, 0xe1,
		0x04, 0x44, 0xa0, 0xe1, 0x23, 0x4c, 0x84, 0xe1, 0x30, 0x50, 0x20, 0xe9,
		0x10, 0x20, 0x52, 0xe2, 0xf3, 0xff, 0xff, 0xaa, 0x30, 0x40, 0xbd, 0xe8,
		0x0c, 0x20, 0x92, 0xe2, 0x05, 0x00, 0x00, 0xba, 0x03, 0xc4, 0xa0, 0xe1,
		0x04, 0x30, 0x31, 0xe5, 0x23, 0xcc, 0x8c, 0xe1, 0x04, 0xc0, 0x20, 0xe5,
		0x04, 0x20, 0x52, 0xe2, 0xf9, 0xff, 0xff, 0xaa, 0x03, 0x10, 0x81, 0xe2,
		0xca, 0xff, 0xff, 0xea, 0x0c, 0x00, 0x52, 0xe3, 0x10, 0x00, 0x00, 0xba,
		0x0c, 0x20, 0x42, 0xe2, 0x30, 0x40, 0x2d, 0xe9, 0x03, 0xe8, 0xa0, 0xe1,
		0x38, 0x10, 0x31, 0xe9, 0x2c, 0xe8, 0x8e, 0xe1, 0x0c, 0xc8, 0xa0, 0xe1,
		0x25, 0xc8, 0x8c, 0xe1, 0x05, 0x58, 0xa0, 0xe1, 0x24, 0x58, 0x85, 0xe1,
		0x04, 0x48, 0xa0, 0xe1, 0x23, 0x48, 0x84, 0xe1, 0x30, 0x50, 0x20, 0xe9,
		0x10, 0x20, 0x52, 0xe2, 0xf3, 0xff, 0xff, 0xaa, 0x30, 0x40, 0xbd, 0xe8,
		0x0c, 0x20, 0x92, 0xe2, 0x05, 0x00, 0x00, 0xba, 0x03, 0xc8, 0xa0, 0xe1,
		0x04, 0x30, 0x31, 0xe5, 0x23, 0xc8, 0x8c, 0xe1, 0x04, 0xc0, 0x20, 0xe5,
		0x04, 0x20, 0x52, 0xe2, 0xf9, 0xff, 0xff, 0xaa, 0x02, 0x10, 0x81, 0xe2,
		0xaf, 0xff, 0xff, 0xea, 0x0c, 0x00, 0x52, 0xe3, 0x10, 0x00, 0x00, 0xba,
		0x0c, 0x20, 0x42, 0xe2, 0x30, 0x40, 0x2d, 0xe9, 0x03, 0xec, 0xa0, 0xe1,
		0x38, 0x10, 0x31, 0xe9, 0x2c, 0xe4, 0x8e, 0xe1, 0x0c, 0xcc, 0xa0, 0xe1,
		0x25, 0xc4, 0x8c, 0xe1, 0x05, 0x5c, 0xa0, 0xe1, 0x24, 0x54, 0x85, 0xe1,
		0x04, 0x4c, 0xa0, 0xe1, 0x23, 0x44, 0x84, 0xe1, 0x30, 0x50, 0x20, 0xe9,
		0x10, 0x20, 0x52, 0xe2, 0xf3, 0xff, 0xff, 0xaa, 0x30, 0x40, 0xbd, 0xe8,
		0x0c, 0x20, 0x92, 0xe2, 0x05, 0x00, 0x00, 0xba, 0x03, 0xcc, 0xa0, 0xe1,
		0x04, 0x30, 0x31, 0xe5, 0x23, 0xc4, 0x8c, 0xe1, 0x04, 0xc0, 0x20, 0xe5,
		0x04, 0x20, 0x52, 0xe2, 0xf9, 0xff, 0xff, 0xaa, 0x01, 0x10, 0x81, 0xe2,
		0x94, 0xff, 0xff, 0xea, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x2d, 0xe9,
		0xff, 0x10, 0x01, 0xe2, 0x04, 0x00, 0x52, 0xe3, 0x1c, 0x00, 0x00, 0xba,
		0x03, 0x30, 0x10, 0xe2, 0x07, 0x00, 0x00, 0x0a, 0x04, 0x30, 0x63, 0xe2,
		0x03, 0x20, 0x42, 0xe0, 0x02, 0x00, 0x53, 0xe3, 0x01, 0x10, 0xc0, 0xe4,
		0x01, 0x10, 0xc0, 0xa4, 0x01, 0x10, 0xc0, 0xc4, 0x04, 0x00, 0x52, 0xe3,
		0x12, 0x00, 0x00, 0xba, 0x01, 0x34, 0x81, 0xe1, 0x03, 0x38, 0x83, 0xe1,
		0x20, 0x00, 0x52, 0xe3, 0x0a, 0x00, 0x00, 0xba, 0x70, 0x00, 0x2d, 0xe9,
		0x03, 0x40, 0xa0, 0xe1, 0x03, 0x50, 0xa0, 0xe1, 0x03, 0x60, 0xa0, 0xe1,
		0x78, 0x00, 0xa0, 0xe8, 0x10, 0x20, 0x42, 0xe2, 0x10, 0x00, 0x52, 0xe3,
		0xfb, 0xff, 0xff, 0xca, 0x70, 0x00, 0xbd, 0xe8, 0x04, 0x00, 0x52, 0xe3,
		0x03, 0x00, 0x00, 0xba, 0x04, 0x30, 0x80, 0xe4, 0x04, 0x20, 0x42, 0xe2,
		0x04, 0x00, 0x52, 0xe3, 0xfb, 0xff, 0xff, 0xaa, 0x00, 0x00, 0x52, 0xe3,
		0x01, 0x00, 0xbd, 0x08, 0x0e, 0xf0, 0xa0, 0x01, 0x02, 0x00, 0x52, 0xe3,
		0x01, 0x10, 0xc0, 0xe4, 0x01, 0x10, 0xc0, 0xa4, 0x01, 0x10, 0xc0, 0xc4,
		0x01, 0x00, 0xbd, 0xe8, 0x0e, 0xf0, 0xa0, 0xe1, 0x88, 0x20, 0x9f, 0xe5,
		0x04, 0xe0, 0x2d, 0xe5, 0x28, 0x18, 0x92, 0xe5, 0x80, 0x30, 0x9f, 0xe5,
		0x0f, 0x10, 0xc1, 0xe3, 0x03, 0x10, 0x81, 0xe3, 0x28, 0x18, 0x82, 0xe5,
		0x28, 0x18, 0x92, 0xe5, 0x00, 0x00, 0xa0, 0xe3, 0xf0, 0x10, 0xc1, 0xe3,
		0x30, 0x10, 0x81, 0xe3, 0x28, 0x18, 0x82, 0xe5, 0x6c, 0x10, 0x92, 0xe5,
		0xf7, 0xe0, 0xa0, 0xe3, 0x01, 0x18, 0x81, 0xe3, 0x6c, 0x10, 0x82, 0xe5,
		0xd8, 0x12, 0x92, 0xe5, 0x0d, 0xc0, 0xa0, 0xe3, 0x01, 0x18, 0x81, 0xe3,
		0xd8, 0x12, 0x82, 0xe5, 0x04, 0x00, 0x83, 0xe5, 0x08, 0xe0, 0x83, 0xe5,
		0x10, 0x00, 0x83, 0xe5, 0x0c, 0x20, 0x93, 0xe5, 0x80, 0x20, 0x82, 0xe3,
		0x0c, 0x20, 0x83, 0xe5, 0x00, 0xc0, 0x83, 0xe5, 0x04, 0x00, 0x83, 0xe5,
		0x0c, 0x20, 0x93, 0xe5, 0x80, 0x20, 0xc2, 0xe3, 0x0c, 0x20, 0x83, 0xe5,
		0x0c, 0x20, 0x93, 0xe5, 0x1f, 0x20, 0xc2, 0xe3, 0x03, 0x20, 0x82, 0xe3,
		0x0c, 0x20, 0x83, 0xe5, 0x04, 0xf0, 0x9d, 0xe4, 0x00, 0x00, 0xc2, 0x01,
		0x00, 0x80, 0xc2, 0x01, 0x10, 0x20, 0x9f, 0xe5, 0x7c, 0x30, 0x92, 0xe5,
		0x02, 0x00, 0x13, 0xe3, 0xfc, 0xff, 0xff, 0x0a, 0x00, 0x00, 0x82, 0xe5,
		0x1e, 0xff, 0x2f, 0xe1, 0x00, 0x80, 0xc2, 0x01, 0xd8, 0x30, 0x9f, 0xe5,
		0xd8, 0x10, 0x9f, 0xe5, 0x64, 0x20, 0xa0, 0xe3, 0x00, 0x12, 0x83, 0xe5,
		0x04, 0x12, 0x83, 0xe5, 0x50, 0x10, 0x93, 0xe5, 0x02, 0x00, 0xa0, 0xe1,
		0x03, 0x18, 0xc1, 0xe3, 0x01, 0x18, 0x81, 0xe3, 0x50, 0x10, 0x83, 0xe5,
		0x01, 0x00, 0x50, 0xe2, 0xfd, 0xff, 0xff, 0x1a, 0xb0, 0x00, 0x9f, 0xe5,
		0x02, 0x10, 0xa0, 0xe1, 0x10, 0x00, 0x83, 0xe5, 0x01, 0x10, 0x51, 0xe2,
		0xfd, 0xff, 0xff, 0x1a, 0xa0, 0x00, 0x9f, 0xe5, 0x02, 0x10, 0xa0, 0xe1,
		0x28, 0x00, 0x83, 0xe5, 0x01, 0x10, 0x51, 0xe2, 0xfd, 0xff, 0xff, 0x1a,
		0xc6, 0x1d, 0xa0, 0xe3, 0x54, 0x10, 0x83, 0xe5, 0x02, 0x10, 0xa0, 0xe1,
		0x01, 0x10, 0x51, 0xe2, 0xfd, 0xff, 0xff, 0x1a, 0x00, 0x11, 0x93, 0xe5,
		0x05, 0x14, 0x81, 0xe3, 0x00, 0x11, 0x83, 0xe5, 0x01, 0x20, 0x52, 0xe2,
		0xfd, 0xff, 0xff, 0x1a, 0x68, 0x20, 0x9f, 0xe5, 0x00, 0x10, 0x93, 0xe5,
		0x03, 0x00, 0xa0, 0xe1, 0x01, 0x20, 0x02, 0xe0, 0x02, 0x21, 0x82, 0xe3,
		0x01, 0x2a, 0x82, 0xe3, 0x00, 0x20, 0x83, 0xe5, 0x50, 0x20, 0x9f, 0xe5,
		0x00, 0x30, 0x93, 0xe5, 0x02, 0x00, 0x00, 0xea, 0x01, 0x20, 0x52, 0xe2,
		0x00, 0x30, 0x90, 0xe5, 0x01, 0x00, 0x00, 0x0a, 0x01, 0x02, 0x13, 0xe3,
		0xfa, 0xff, 0xff, 0x0a, 0x1c, 0x10, 0x9f, 0xe5, 0x64, 0x20, 0xa0, 0xe3,
		0x50, 0x30, 0x91, 0xe5, 0x03, 0x38, 0xc3, 0xe3, 0x02, 0x38, 0x83, 0xe3,
		0x50, 0x30, 0x81, 0xe5, 0x01, 0x20, 0x52, 0xe2, 0xfd, 0xff, 0xff, 0x1a,
		0x1e, 0xff, 0x2f, 0xe1, 0x00, 0x00, 0xc2, 0x01, 0xff, 0x01, 0x00, 0x00,
		0x07, 0x41, 0x00, 0x81, 0x00, 0x18, 0x04, 0x80, 0xcc, 0xe0, 0xfc, 0xff,
		0xfe, 0x0f, 0x00, 0x00, 0xd8, 0x30, 0x9f, 0xe5, 0x00, 0x20, 0x93, 0xe5,
		0xa2, 0x22, 0xa0, 0xe1, 0x0f, 0x20, 0x02, 0xe2, 0x0c, 0x00, 0x52, 0xe3,
		0x16, 0x00, 0x00, 0x0a, 0x0b, 0x00, 0x52, 0xe3, 0x03, 0x00, 0x00, 0x0a,
		0x00, 0x30, 0xa0, 0xe3, 0xb4, 0x20, 0x9f, 0xe5, 0x10, 0x30, 0x82, 0xe5,
		0x1e, 0xff, 0x2f, 0xe1, 0xac, 0x30, 0x9f, 0xe5, 0x03, 0x00, 0x50, 0xe1,
		0x23, 0x00, 0x00, 0x3a, 0x20, 0x32, 0xa0, 0xe1, 0xa0, 0x31, 0x83, 0xe0,
		0x9c, 0x20, 0x9f, 0xe5, 0x00, 0x30, 0x83, 0xe0, 0xa0, 0x02, 0x83, 0xe0,
		0x02, 0x00, 0x50, 0xe1, 0xf1, 0xff, 0xff, 0x9a, 0x8c, 0x10, 0x9f, 0xe5,
		0x00, 0x30, 0xa0, 0xe3, 0x01, 0x00, 0x80, 0xe0, 0x02, 0x00, 0x50, 0xe1,
		0x01, 0x30, 0x83, 0xe2, 0xfb, 0xff, 0xff, 0x8a, 0xeb, 0xff, 0xff, 0xea,
		0x68, 0x30, 0x9f, 0xe5, 0x03, 0x00, 0x50, 0xe1, 0x0d, 0x00, 0x00, 0x3a,
		0x20, 0x32, 0xa0, 0xe1, 0xa0, 0x31, 0x83, 0xe0, 0x60, 0x20, 0x9f, 0xe5,
		0x00, 0x30, 0x83, 0xe0, 0xa0, 0x02, 0x83, 0xe0, 0x02, 0x00, 0x50, 0xe1,
		0xe0, 0xff, 0xff, 0x9a, 0x50, 0x10, 0x9f, 0xe5, 0x00, 0x30, 0xa0, 0xe3,
		0x01, 0x00, 0x80, 0xe0, 0x02, 0x00, 0x50, 0xe1, 0x01, 0x30, 0x83, 0xe2,
		0xfb, 0xff, 0xff, 0x8a, 0xda, 0xff, 0xff, 0xea, 0x80, 0x22, 0x60, 0xe0,
		0x82, 0x21, 0x80, 0xe0, 0x82, 0x30, 0x80, 0xe0, 0x23, 0x33, 0xa0, 0xe1,
		0xd5, 0xff, 0xff, 0xea, 0x80, 0x32, 0x60, 0xe0, 0x83, 0x31, 0x80, 0xe0,
		0x83, 0x30, 0x80, 0xe0, 0xa3, 0x32, 0xa0, 0xe1, 0xd0, 0xff, 0xff, 0xea,
		0x00, 0x10, 0xc0, 0x01, 0x40, 0x42, 0x0f, 0x00, 0x2c, 0x31, 0x01, 0x00,
		0xd3, 0xce, 0xfe, 0xff, 0x59, 0x62, 0x02, 0x00, 0xa6, 0x9d, 0xfd, 0xff,
		0x28, 0x30, 0x90, 0xe5, 0x30, 0x40, 0x2d, 0xe9, 0x18, 0x50, 0xd0, 0xe5,
		0x23, 0x31, 0xa0, 0xe1, 0x83, 0x41, 0xa0, 0xe1, 0x1c, 0x30, 0x90, 0xe5,
		0x14, 0x20, 0x90, 0xe5, 0x00, 0x00, 0x55, 0xe3, 0x10, 0xe0, 0x90, 0xe5,
		0x24, 0xc0, 0x90, 0xe5, 0x20, 0x10, 0x90, 0xe5, 0x23, 0x32, 0xa0, 0x11,
		0x0c, 0x00, 0x90, 0xe5, 0xa3, 0x32, 0xa0, 0x01, 0x83, 0x36, 0xa0, 0xe1,
		0x03, 0x20, 0x82, 0xe1, 0x80, 0x37, 0x82, 0xe1, 0xae, 0xe0, 0xa0, 0xe1,
		0x01, 0xc0, 0x4c, 0xe2, 0x0e, 0xe2, 0xa0, 0xe1, 0x04, 0x30, 0x83, 0xe1,
		0x01, 0x10, 0x41, 0xe2, 0x8c, 0xc2, 0xa0, 0xe1, 0x0e, 0x30, 0x83, 0xe1,
		0x81, 0x14, 0xa0, 0xe1, 0x0c, 0x30, 0x83, 0xe1, 0x01, 0x30, 0x83, 0xe1,
		0x48, 0x10, 0x9f, 0xe5, 0x05, 0x38, 0x83, 0xe1, 0x02, 0x30, 0x83, 0xe3,
		0x00, 0x30, 0x81, 0xe5, 0x0c, 0x30, 0x91, 0xe5, 0x01, 0x20, 0xa0, 0xe1,
		0x02, 0x37, 0x83, 0xe3, 0x0c, 0x30, 0x81, 0xe5, 0x0c, 0x30, 0x91, 0xe5,
		0xff, 0x04, 0xe0, 0xe3, 0x01, 0x30, 0x83, 0xe3, 0x0c, 0x30, 0x81, 0xe5,
		0x01, 0x00, 0x00, 0xea, 0x01, 0x00, 0x50, 0xe2, 0x30, 0x80, 0xbd, 0x08,
		0x0c, 0x30, 0x92, 0xe5, 0x01, 0x00, 0x13, 0xe3, 0xfa, 0xff, 0xff, 0x1a,
		0x01, 0x00, 0xa0, 0xe3, 0x30, 0x80, 0xbd, 0xe8, 0x00, 0x10, 0xc0, 0x01,
		0x01, 0x00, 0x50, 0xe3, 0x48, 0x00, 0x00, 0x1a, 0xb0, 0xc1, 0x9f, 0xe5,
		0xf0, 0x40, 0x2d, 0xe9, 0x00, 0x50, 0xa0, 0xe3, 0x05, 0x40, 0xa0, 0xe1,
		0x05, 0xe0, 0xa0, 0xe1, 0x0c, 0x20, 0x9c, 0xe5, 0xff, 0x34, 0xe0, 0xe3,
		0x07, 0x2d, 0xc2, 0xe3, 0x0e, 0x23, 0x82, 0xe1, 0x0c, 0x20, 0x8c, 0xe5,
		0x24, 0x20, 0x9c, 0xe5, 0x01, 0x20, 0x82, 0xe3, 0x24, 0x20, 0x8c, 0xe5,
		0x01, 0x00, 0x00, 0xea, 0x01, 0x30, 0x53, 0xe2, 0x02, 0x00, 0x00, 0x0a,
		0x24, 0x20, 0x9c, 0xe5, 0x01, 0x00, 0x12, 0xe3, 0xfa, 0xff, 0xff, 0x1a,
		0x24, 0x30, 0x9c, 0xe5, 0x30, 0x00, 0x13, 0xe3, 0x02, 0x00, 0x00, 0x1a,
		0x24, 0x00, 0x9c, 0xe5, 0x10, 0x00, 0x10, 0xe2, 0x14, 0x00, 0x00, 0x0a,
		0x01, 0xe0, 0x8e, 0xe2, 0x08, 0x00, 0x5e, 0xe3, 0xe8, 0xff, 0xff, 0x1a,
		0x40, 0x21, 0x9f, 0xe5, 0xff, 0x34, 0xe0, 0xe3, 0x0c, 0x00, 0x92, 0xe5,
		0x02, 0x10, 0xa0, 0xe1, 0x07, 0x0d, 0xc0, 0xe3, 0x05, 0x53, 0x80, 0xe1,
		0x0c, 0x50, 0x82, 0xe5, 0x24, 0x00, 0x92, 0xe5, 0x01, 0x00, 0x80, 0xe3,
		0x24, 0x00, 0x82, 0xe5, 0x01, 0x00, 0x00, 0xea, 0x01, 0x30, 0x53, 0xe2,
		0x02, 0x00, 0x00, 0x0a, 0x24, 0x20, 0x91, 0xe5, 0x01, 0x00, 0x12, 0xe3,
		0xfa, 0xff, 0xff, 0x1a, 0x00, 0x00, 0xa0, 0xe3, 0xf0, 0x80, 0xbd, 0xe8,
		0x00, 0x30, 0x91, 0xe5, 0x00, 0x60, 0xa0, 0xe1, 0x10, 0x00, 0x53, 0xe3,
		0x04, 0x70, 0xa0, 0x03, 0x02, 0x70, 0xa0, 0x13, 0x00, 0x00, 0x56, 0xe3,
		0x30, 0x20, 0x9c, 0x05, 0x05, 0x00, 0x00, 0x0a, 0x01, 0x00, 0x56, 0xe3,
		0x34, 0x20, 0x9c, 0x05, 0x02, 0x00, 0x00, 0x0a, 0x02, 0x00, 0x56, 0xe3,
		0x38, 0x20, 0x9c, 0x05, 0x3c, 0x20, 0x9c, 0x15, 0x20, 0x30, 0xa0, 0xe3,
		0x01, 0x00, 0x12, 0xe3, 0x01, 0x00, 0x80, 0x12, 0x01, 0x30, 0x53, 0xe2,
		0xa2, 0x20, 0xa0, 0xe1, 0xfa, 0xff, 0xff, 0x1a, 0x01, 0x60, 0x86, 0xe2,
		0x07, 0x00, 0x56, 0xe1, 0xed, 0xff, 0xff, 0x1a, 0x04, 0x00, 0x50, 0xe1,
		0x00, 0x40, 0xa0, 0x81, 0x0e, 0x50, 0xa0, 0x81, 0xcf, 0xff, 0xff, 0xea,
		0x8c, 0x30, 0x9f, 0xe5, 0x00, 0x00, 0xa0, 0xe3, 0x00, 0x20, 0x93, 0xe5,
		0x03, 0xc0, 0xa0, 0xe1, 0x16, 0x2a, 0xc2, 0xe3, 0x00, 0x20, 0x83, 0xe5,
		0x0c, 0x10, 0x9c, 0xe5, 0x02, 0x21, 0xa0, 0xe3, 0x07, 0x1d, 0xc1, 0xe3,
		0x00, 0x10, 0x81, 0xe1, 0x00, 0x30, 0xa0, 0xe3, 0x0c, 0x10, 0x8c, 0xe5,
		0x04, 0x30, 0x82, 0xe4, 0x01, 0x30, 0x83, 0xe2, 0x20, 0x00, 0x53, 0xe3,
		0xfb, 0xff, 0xff, 0x1a, 0x02, 0x21, 0xa0, 0xe3, 0x00, 0x30, 0xa0, 0xe3,
		0x01, 0x00, 0x00, 0xea, 0x20, 0x00, 0x53, 0xe3, 0x08, 0x00, 0x00, 0x0a,
		0x00, 0x10, 0x92, 0xe5, 0x04, 0x20, 0x82, 0xe2, 0x03, 0x00, 0x51, 0xe1,
		0x01, 0x30, 0x83, 0xe2, 0xf8, 0xff, 0xff, 0x0a, 0x40, 0x00, 0x80, 0xe2,
		0x02, 0x0c, 0x50, 0xe3, 0xe8, 0xff, 0xff, 0x1a, 0x00, 0x00, 0xa0, 0xe3,
		0x14, 0x20, 0x9f, 0xe5, 0x0c, 0x30, 0x92, 0xe5, 0x07, 0x3d, 0xc3, 0xe3,
		0x00, 0x00, 0x83, 0xe1, 0x0c, 0x00, 0x82, 0xe5, 0x00, 0x00, 0xa0, 0xe3,
		0x1e, 0xff, 0x2f, 0xe1, 0x00, 0x10, 0xc0, 0x01, 0x30, 0x40, 0x2d, 0xe9,
		0x02, 0x01, 0xa0, 0xe3, 0x34, 0xd0, 0x4d, 0xe2, 0x00, 0x10, 0xa0, 0xe3,
		0x9c, 0x40, 0xa0, 0xe3, 0x01, 0x50, 0xa0, 0xe3, 0x01, 0x30, 0xa0, 0xe3,
		0xf0, 0x00, 0xcd, 0xe1, 0xf8, 0x40, 0xcd, 0xe1, 0x18, 0x30, 0xcd, 0xe5,
		0x10, 0xe0, 0xa0, 0xe3, 0x0a, 0xc0, 0xa0, 0xe3, 0x0d, 0x00, 0xa0, 0xe3,
		0x01, 0x40, 0xa0, 0xe3, 0x00, 0x50, 0xa0, 0xe3, 0x04, 0x10, 0xa0, 0xe3,
		0x03, 0x20, 0xa0, 0xe3, 0x00, 0x30, 0xa0, 0xe3, 0xf0, 0x41, 0xcd, 0xe1,
		0x1c, 0xe0, 0x8d, 0xe5, 0x20, 0xc0, 0x8d, 0xe5, 0x24, 0x00, 0x8d, 0xe5,
		0x28, 0x10, 0x8d, 0xe5, 0x2c, 0x20, 0x8d, 0xe5, 0x5c, 0x30, 0x93, 0xe5,
		0x23, 0x3c, 0xa0, 0xe1, 0x58, 0x00, 0x53, 0xe3, 0xd3, 0x00, 0x00, 0x0a,
		0x00, 0x24, 0x9f, 0xe5, 0x00, 0x34, 0x9f, 0xe5, 0x24, 0x18, 0x92, 0xe5,
		0x07, 0x1a, 0x81, 0xe3, 0x24, 0x18, 0x82, 0xe5, 0x01, 0x30, 0x53, 0xe2,
		0xfd, 0xff, 0xff, 0x1a, 0x2c, 0x30, 0x9d, 0xe5, 0x08, 0x00, 0x13, 0xe3,
		0xc4, 0x3a, 0x92, 0x15, 0x03, 0x35, 0x83, 0x13, 0xc4, 0x3a, 0x82, 0x15,
		0x08, 0x30, 0x9d, 0xe5, 0x90, 0x20, 0x43, 0xe2, 0x24, 0x00, 0x52, 0xe3,
		0xc4, 0x33, 0x9f, 0x95, 0xc8, 0x23, 0x9f, 0x95, 0xc0, 0x2a, 0x83, 0x95,
		0x08, 0x30, 0x9d, 0x95, 0xb3, 0x00, 0x53, 0xe3, 0xb0, 0x33, 0x9f, 0x85,
		0xb8, 0x23, 0x9f, 0x85, 0xc0, 0x2a, 0x83, 0x85, 0x08, 0x30, 0x9d, 0x85,
		0xb0, 0x23, 0x9f, 0xe5, 0x60, 0x00, 0x53, 0xe3, 0x83, 0x30, 0xa0, 0xe1,
		0x92, 0x13, 0x83, 0xe0, 0x2c, 0x20, 0x9d, 0xe5, 0xa3, 0x31, 0xa0, 0x91,
		0x23, 0x32, 0xa0, 0x81, 0x01, 0x30, 0x43, 0x92, 0x01, 0x30, 0x43, 0x82,
		0x03, 0x34, 0xa0, 0x91, 0x03, 0x34, 0xa0, 0x81, 0x06, 0x31, 0x83, 0x93,
		0x02, 0x31, 0x83, 0x83, 0x10, 0x00, 0x12, 0xe3, 0xae, 0x00, 0x00, 0x0a,
		0x64, 0x23, 0x9f, 0xe5, 0x74, 0x13, 0x9f, 0xe5, 0x90, 0x12, 0x82, 0xe5,
		0x2c, 0x20, 0x9d, 0xe5, 0xf0, 0x00, 0x12, 0xe3, 0x50, 0x23, 0x9f, 0xe5,
		0x01, 0x34, 0x83, 0x13, 0x20, 0x30, 0x82, 0xe5, 0x20, 0x10, 0x92, 0xe5,
		0x02, 0x30, 0xa0, 0xe1, 0x01, 0x16, 0x81, 0xe3, 0x20, 0x10, 0x82, 0xe5,
		0x20, 0x20, 0x93, 0xe5, 0x01, 0x02, 0x12, 0xe3, 0xfc, 0xff, 0xff, 0x0a,
		0x2c, 0x23, 0x9f, 0xe5, 0x01, 0x20, 0x52, 0xe2, 0xfd, 0xff, 0xff, 0x1a,
		0x60, 0x20, 0x93, 0xe5, 0x30, 0x43, 0x9f, 0xe5, 0x01, 0x29, 0x82, 0xe3,
		0x60, 0x20, 0x83, 0xe5, 0xc0, 0x22, 0x93, 0xe5, 0x24, 0x13, 0x9f, 0xe5,
		0x01, 0x29, 0xc2, 0xe3, 0xc0, 0x22, 0x83, 0xe5, 0xc0, 0x22, 0x93, 0xe5,
		0xf8, 0x02, 0x9f, 0xe5, 0x01, 0x29, 0x82, 0xe3, 0xc0, 0x22, 0x83, 0xe5,
		0x18, 0x20, 0xdd, 0xe5, 0xc4, 0x3a, 0x93, 0xe5, 0x01, 0x00, 0x52, 0xe3,
		0x00, 0x23, 0x9f, 0xe5, 0x01, 0x38, 0x83, 0x03, 0x01, 0x38, 0xc3, 0x13,
		0xc4, 0x3a, 0x80, 0xe5, 0x04, 0x10, 0x84, 0xe5, 0x0d, 0x00, 0xa0, 0xe1,
		0x08, 0x20, 0x84, 0xe5, 0xf2, 0xfe, 0xff, 0xeb, 0x00, 0x10, 0xa0, 0xe3,
		0x04, 0x30, 0xa0, 0xe1, 0x01, 0xc0, 0xa0, 0xe1, 0x0c, 0x20, 0x93, 0xe5,
		0xff, 0x04, 0xe0, 0xe3, 0x07, 0x2d, 0xc2, 0xe3, 0x01, 0x20, 0x82, 0xe1,
		0x0c, 0x20, 0x83, 0xe5, 0x24, 0x20, 0x93, 0xe5, 0x01, 0x20, 0x82, 0xe3,
		0x24, 0x20, 0x83, 0xe5, 0x01, 0x00, 0x00, 0xea, 0x01, 0x00, 0x50, 0xe2,
		0x02, 0x00, 0x00, 0x0a, 0x24, 0x20, 0x93, 0xe5, 0x01, 0x00, 0x12, 0xe3,
		0xfa, 0xff, 0xff, 0x1a, 0x24, 0x20, 0x93, 0xe5, 0x40, 0x10, 0x81, 0xe2,
		0x30, 0x00, 0x12, 0xe3, 0x01, 0xc0, 0x8c, 0x12, 0x02, 0x0c, 0x51, 0xe3,
		0xeb, 0xff, 0xff, 0x1a, 0x64, 0x32, 0x9f, 0xe5, 0x08, 0x00, 0x5c, 0xe3,
		0x01, 0x20, 0xa0, 0x13, 0x00, 0x20, 0xa0, 0x03, 0x18, 0x20, 0xcd, 0xe5,
		0xc4, 0x3a, 0x93, 0xe5, 0x4c, 0x22, 0x9f, 0xe5, 0x01, 0x38, 0x83, 0x13,
		0x01, 0x38, 0xc3, 0x03, 0xc4, 0x3a, 0x82, 0xe5, 0x08, 0x00, 0x9d, 0xe5,
		0x80, 0x32, 0x60, 0xe0, 0x03, 0x33, 0x63, 0xe0, 0x83, 0x01, 0x80, 0xe0,
		0x00, 0x03, 0xa0, 0xe1, 0x8d, 0xfe, 0xff, 0xeb, 0x1c, 0x10, 0x8d, 0xe2,
		0x18, 0x00, 0xdd, 0xe5, 0xf8, 0xfe, 0xff, 0xeb, 0x0a, 0x20, 0xa0, 0xe3,
		0x0d, 0x30, 0xa0, 0xe3, 0x0d, 0x00, 0xa0, 0xe1, 0xf0, 0x22, 0xcd, 0xe1,
		0xc3, 0xfe, 0xff, 0xeb, 0x1c, 0x10, 0x8d, 0xe2, 0x18, 0x00, 0xdd, 0xe5,
		0xf0, 0xfe, 0xff, 0xeb, 0x1c, 0x32, 0x9f, 0xe5, 0x1c, 0x02, 0x9f, 0xe5,
		0x1c, 0x12, 0x9f, 0xe5, 0x1c, 0x22, 0x9f, 0xe5, 0x00, 0x00, 0x83, 0xe5,
		0x00, 0x14, 0x83, 0xe5, 0x01, 0x30, 0x83, 0xe2, 0x02, 0x00, 0x53, 0xe1,
		0xfa, 0xff, 0xff, 0x1a, 0xf8, 0x31, 0x9f, 0xe5, 0xfc, 0xc1, 0x9f, 0xe5,
		0xfc, 0x01, 0x9f, 0xe5, 0x00, 0x20, 0xa0, 0xe3, 0x00, 0x10, 0x93, 0xe5,
		0x01, 0x30, 0x83, 0xe2, 0x0c, 0x00, 0x51, 0xe1, 0x01, 0x20, 0x82, 0x02,
		0x00, 0x00, 0x53, 0xe1, 0xf9, 0xff, 0xff, 0x1a, 0x20, 0x00, 0x52, 0xe3,
		0x53, 0x00, 0x00, 0x0a, 0x0d, 0x30, 0xa0, 0xe3, 0x0a, 0x20, 0xa0, 0xe3,
		0x0d, 0x00, 0xa0, 0xe1, 0xf0, 0x22, 0xcd, 0xe1, 0xa6, 0xfe, 0xff, 0xeb,
		0xc4, 0x11, 0x9f, 0xe5, 0xc4, 0x31, 0x9f, 0xe5, 0x0a, 0xc0, 0xa0, 0xe3,
		0xc0, 0x41, 0x9f, 0xe5, 0xc0, 0xe1, 0x9f, 0xe5, 0x03, 0x10, 0x41, 0xe0,
		0x20, 0x00, 0x83, 0xe2, 0x03, 0x20, 0xa0, 0xe1, 0x00, 0x40, 0x82, 0xe5,
		0x02, 0xe0, 0x81, 0xe7, 0x01, 0x20, 0x82, 0xe2, 0x00, 0x00, 0x52, 0xe1,
		0xfa, 0xff, 0xff, 0x1a, 0x9c, 0xe1, 0x9f, 0xe5, 0x00, 0x20, 0xa0, 0xe3,
		0x00, 0x10, 0x93, 0xe5, 0x01, 0x30, 0x83, 0xe2, 0x0e, 0x00, 0x51, 0xe1,
		0x01, 0x20, 0x82, 0x02, 0x00, 0x00, 0x53, 0xe1, 0xf9, 0xff, 0xff, 0x1a,
		0x20, 0x00, 0x52, 0xe3, 0x2b, 0x00, 0x00, 0x0a, 0x0d, 0x30, 0xa0, 0xe3,
		0x0a, 0x00, 0x5c, 0xe3, 0x24, 0x30, 0x8d, 0xe5, 0x20, 0x30, 0xa0, 0x13,
		0x20, 0xc0, 0x8d, 0xe5, 0x04, 0x30, 0x8d, 0x15, 0x44, 0x00, 0x00, 0x0a,
		0x08, 0x00, 0x9d, 0xe5, 0x00, 0x40, 0xa0, 0xe3, 0x80, 0x32, 0x60, 0xe0,
		0x03, 0x33, 0x63, 0xe0, 0x83, 0x01, 0x80, 0xe0, 0x00, 0x03, 0xa0, 0xe1,
		0x43, 0xfe, 0xff, 0xeb, 0x0d, 0x00, 0xa0, 0xe1, 0x0c, 0x40, 0x8d, 0xe5,
		0x7e, 0xfe, 0xff, 0xeb, 0x04, 0x30, 0xa0, 0xe1, 0x00, 0x20, 0x9d, 0xe5,
		0x02, 0x10, 0x83, 0xe0, 0x02, 0x10, 0x83, 0xe7, 0x04, 0x30, 0x83, 0xe2,
		0x02, 0x0c, 0x53, 0xe3, 0xf9, 0xff, 0xff, 0x1a, 0x00, 0x30, 0x9d, 0xe5,
		0x02, 0x1c, 0x83, 0xe2, 0x01, 0x00, 0x00, 0xea, 0x03, 0x00, 0x51, 0xe1,
		0x14, 0x00, 0x00, 0x0a, 0x00, 0x20, 0x93, 0xe5, 0x03, 0x00, 0x52, 0xe1,
		0x04, 0x30, 0x83, 0xe2, 0xf9, 0xff, 0xff, 0x0a, 0x34, 0xd0, 0x8d, 0xe2,
		0x30, 0x80, 0xbd, 0xe8, 0x20, 0x00, 0x12, 0xe3, 0x1b, 0x00, 0x00, 0x1a,
		0x40, 0x00, 0x12, 0xe3, 0x1e, 0x00, 0x00, 0x0a, 0x98, 0x20, 0x9f, 0xe5,
		0xd8, 0x10, 0x9f, 0xe5, 0x90, 0x12, 0x82, 0xe5, 0x2c, 0x20, 0x9d, 0xe5,
		0x4b, 0xff, 0xff, 0xea, 0x0c, 0x20, 0xa0, 0xe3, 0x10, 0x30, 0xa0, 0xe3,
		0x20, 0xc0, 0x8d, 0xe5, 0x24, 0x20, 0x8d, 0xe5, 0x04, 0x30, 0x8d, 0xe5,
		0xd4, 0xff, 0xff, 0xea, 0x04, 0x30, 0x9d, 0xe5, 0x00, 0x20, 0xa0, 0xe3,
		0x16, 0x33, 0x83, 0xe3, 0x5c, 0x30, 0x82, 0xe5, 0x34, 0xd0, 0x8d, 0xe2,
		0x30, 0x80, 0xbd, 0xe8, 0x0d, 0x30, 0xa0, 0xe3, 0x09, 0x20, 0xa0, 0xe3,
		0x0d, 0x00, 0xa0, 0xe1, 0xf0, 0x22, 0xcd, 0xe1, 0x52, 0xfe, 0xff, 0xeb,
		0x88, 0x10, 0x9f, 0xe5, 0x88, 0x30, 0x9f, 0xe5, 0x09, 0xc0, 0xa0, 0xe3,
		0xaa, 0xff, 0xff, 0xea, 0x30, 0x20, 0x9f, 0xe5, 0x7c, 0x10, 0x9f, 0xe5,
		0x90, 0x12, 0x82, 0xe5, 0x2c, 0x20, 0x9d, 0xe5, 0x31, 0xff, 0xff, 0xea,
		0x80, 0x00, 0x12, 0xe3, 0x18, 0x20, 0x9f, 0x15, 0x68, 0x10, 0x9f, 0x15,
		0x90, 0x12, 0x82, 0x15, 0x2c, 0x20, 0x9d, 0x15, 0x2b, 0xff, 0xff, 0xea,
		0x40, 0x30, 0xa0, 0xe3, 0x04, 0x30, 0x8d, 0xe5, 0xb7, 0xff, 0xff, 0xea,
		0x00, 0x00, 0xc2, 0x01, 0x10, 0x27, 0x00, 0x00, 0xaa, 0x0a, 0x00, 0x00,
		0xff, 0x0f, 0x00, 0x00, 0xab, 0xaa, 0xaa, 0xaa, 0x33, 0x33, 0x30, 0xd1,
		0x00, 0x10, 0xc0, 0x01, 0xc2, 0xed, 0xce, 0xb7, 0x08, 0x00, 0xa7, 0x00,
		0x00, 0x02, 0x00, 0x80, 0x11, 0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x22,
		0x20, 0x02, 0x00, 0x80, 0x00, 0x00, 0xc0, 0x80, 0x00, 0x00, 0x40, 0x80,
		0x33, 0x33, 0x33, 0x33, 0x44, 0x44, 0x44, 0x44, 0x99, 0x99, 0x90, 0xc8,
		0x00, 0x00, 0x60, 0x80, 0x00, 0x00, 0x20, 0x80, 0x66, 0x66, 0xe0, 0xcc,
		0xcc, 0xcc, 0x40, 0xc4
	};
	fel_write(ctx, 0x00008800, (void *)&payload[0], sizeof(payload));
	fel_exec(ctx, 0x00008800);    
    usleep(100000);                                                                 // Wait 100ms for sdram init in SoC (Otherwise it might cause USB bulk error)
    sdram_initialized=1;
	return 1;
}

static int chip_spi_init(struct xfel_ctx_t * ctx, uint32_t * swapbuf, uint32_t * swaplen, uint32_t * cmdlen)
{
	static const uint8_t payload[] = {    
		0xff, 0xff, 0xff, 0xea, 0x40, 0x00, 0xa0, 0xe3, 0x00, 0xd0, 0x80, 0xe5,
		0x04, 0xe0, 0x80, 0xe5, 0x00, 0xe0, 0x0f, 0xe1, 0x08, 0xe0, 0x80, 0xe5,
		0x10, 0xef, 0x11, 0xee, 0x0c, 0xe0, 0x80, 0xe5, 0x10, 0xef, 0x11, 0xee,
     // 0x10, 0xe0, 0x80, 0xe5, 0x26, 0x0b, 0xa0, 0xe3, 0x8b, 0x00, 0x00, 0xeb,     // Original, executing cmdbuf at 0x9800
		0x10, 0xe0, 0x80, 0xe5, 0x02, 0x01, 0xa0, 0xe3, 0x85, 0x00, 0x00, 0xeb,     // Change cmdbuf address to SDRAM base (0x80000000) to allow much larger queues
		0x04, 0x00, 0xa0, 0xe3, 0x65, 0x10, 0xa0, 0xe3, 0x00, 0x10, 0xc0, 0xe5,
		0x47, 0x10, 0xa0, 0xe3, 0x01, 0x10, 0xc0, 0xe5, 0x4f, 0x10, 0xa0, 0xe3,
		0x02, 0x10, 0xc0, 0xe5, 0x4e, 0x10, 0xa0, 0xe3, 0x03, 0x10, 0xc0, 0xe5,
		0x2e, 0x10, 0xa0, 0xe3, 0x04, 0x10, 0xc0, 0xe5, 0x46, 0x10, 0xa0, 0xe3,
		0x05, 0x10, 0xc0, 0xe5, 0x45, 0x10, 0xa0, 0xe3, 0x06, 0x10, 0xc0, 0xe5,
		0x4c, 0x10, 0xa0, 0xe3, 0x07, 0x10, 0xc0, 0xe5, 0x40, 0x00, 0xa0, 0xe3,
		0x00, 0xd0, 0x90, 0xe5, 0x04, 0xe0, 0x90, 0xe5, 0x10, 0x10, 0x90, 0xe5,
		0x10, 0x1f, 0x01, 0xee, 0x0c, 0x10, 0x90, 0xe5, 0x10, 0x1f, 0x01, 0xee,
		0x08, 0x10, 0x90, 0xe5, 0x01, 0xf0, 0x29, 0xe1, 0x1e, 0xff, 0x2f, 0xe1,
		0xf0, 0x40, 0x2d, 0xe9, 0x00, 0x50, 0x51, 0xe2, 0xf0, 0x80, 0xbd, 0x08,
		0xc8, 0xe0, 0x9f, 0xe5, 0xc8, 0x60, 0x9f, 0xe5, 0xc8, 0x40, 0x9f, 0xe5,
		0x00, 0x70, 0xe0, 0xe3, 0x40, 0x00, 0x55, 0xe3, 0x05, 0x20, 0xa0, 0x31,
		0x40, 0x20, 0xa0, 0x23, 0x00, 0x30, 0xa0, 0xe3, 0x07, 0x10, 0xa0, 0xe1,
		0x30, 0x20, 0x8e, 0xe5, 0x34, 0x20, 0x8e, 0xe5, 0x38, 0x20, 0x8e, 0xe5,
		0x01, 0x30, 0x83, 0xe2, 0x03, 0x00, 0x52, 0xe1, 0x00, 0x10, 0xc6, 0xe5,
		0xfb, 0xff, 0xff, 0xca, 0x08, 0x30, 0x9e, 0xe5, 0x02, 0x31, 0x83, 0xe3,
		0x08, 0x30, 0x8e, 0xe5, 0x1c, 0x30, 0x9e, 0xe5, 0xff, 0x30, 0x03, 0xe2,
		0x03, 0x00, 0x52, 0xe1, 0xfb, 0xff, 0xff, 0x8a, 0x00, 0x30, 0xa0, 0xe3,
		0x00, 0xc0, 0xd4, 0xe5, 0x00, 0x00, 0x50, 0xe3, 0x00, 0x10, 0xa0, 0xe1,
		0xff, 0xc0, 0x0c, 0xe2, 0x07, 0x00, 0x00, 0x0a, 0x01, 0x30, 0x83, 0xe2,
		0x03, 0x00, 0x52, 0xe1, 0x01, 0xc0, 0xc1, 0xe4, 0x01, 0x00, 0xa0, 0xe1,
		0xf5, 0xff, 0xff, 0xca, 0x02, 0x50, 0x55, 0xe0, 0xdf, 0xff, 0xff, 0x1a,
		0xf0, 0x80, 0xbd, 0xe8, 0x01, 0x10, 0x83, 0xe2, 0x02, 0x00, 0x51, 0xe1,
		0x02, 0x30, 0x83, 0xe2, 0xf8, 0xff, 0xff, 0xaa, 0x03, 0x00, 0x52, 0xe1,
		0x00, 0x10, 0xd4, 0xe5, 0xf5, 0xff, 0xff, 0xda, 0x00, 0x10, 0xd4, 0xe5,
		0x01, 0x10, 0x83, 0xe2, 0x02, 0x00, 0x51, 0xe1, 0x02, 0x30, 0x83, 0xe2,
		0xf7, 0xff, 0xff, 0xba, 0x02, 0x50, 0x55, 0xe0, 0xd0, 0xff, 0xff, 0x1a,
		0xf0, 0x80, 0xbd, 0xe8, 0x00, 0x50, 0xc0, 0x01, 0x00, 0x52, 0xc0, 0x01,
		0x00, 0x53, 0xc0, 0x01, 0xf0, 0x40, 0x2d, 0xe9, 0x00, 0x60, 0x51, 0xe2,
		0xf0, 0x80, 0xbd, 0x08, 0xa4, 0x10, 0x9f, 0xe5, 0xa4, 0x50, 0x9f, 0xe5,
		0xa4, 0x40, 0x9f, 0xe5, 0x00, 0x70, 0xe0, 0xe3, 0x40, 0x00, 0x56, 0xe3,
		0x06, 0x20, 0xa0, 0x31, 0x40, 0x20, 0xa0, 0x23, 0x00, 0x00, 0x50, 0xe3,
		0x30, 0x20, 0x81, 0xe5, 0x34, 0x20, 0x81, 0xe5, 0x38, 0x20, 0x81, 0xe5,
		0x17, 0x00, 0x00, 0x0a, 0x00, 0xc0, 0xa0, 0xe1, 0x00, 0x30, 0xa0, 0xe3,
		0x01, 0xe0, 0xdc, 0xe4, 0x01, 0x30, 0x83, 0xe2, 0x03, 0x00, 0x52, 0xe1,
		0x00, 0xe0, 0xc5, 0xe5, 0xfa, 0xff, 0xff, 0xca, 0x08, 0x30, 0x91, 0xe5,
		0x02, 0x31, 0x83, 0xe3, 0x08, 0x30, 0x81, 0xe5, 0x1c, 0x30, 0x91, 0xe5,
		0xff, 0x30, 0x03, 0xe2, 0x03, 0x00, 0x52, 0xe1, 0xfb, 0xff, 0xff, 0x8a,
		0x00, 0x30, 0xa0, 0xe3, 0x01, 0x30, 0x83, 0xe2, 0x03, 0x00, 0x52, 0xe1,
		0x00, 0xc0, 0xd4, 0xe5, 0xfb, 0xff, 0xff, 0xca, 0x00, 0x00, 0x50, 0xe3,
		0x02, 0x00, 0x80, 0x10, 0x02, 0x60, 0x56, 0xe0, 0xe0, 0xff, 0xff, 0x1a,
		0xf0, 0x80, 0xbd, 0xe8, 0x00, 0x30, 0xa0, 0xe1, 0x07, 0xc0, 0xa0, 0xe1,
		0x01, 0x30, 0x83, 0xe2, 0x03, 0x00, 0x52, 0xe1, 0x00, 0xc0, 0xc5, 0xe5,
		0xfb, 0xff, 0xff, 0xca, 0xe7, 0xff, 0xff, 0xea, 0x00, 0x50, 0xc0, 0x01,
		0x00, 0x52, 0xc0, 0x01, 0x00, 0x53, 0xc0, 0x01, 0xf0, 0x43, 0x2d, 0xe9,
		0x50, 0x82, 0x9f, 0xe5, 0x50, 0x52, 0x9f, 0xe5, 0x50, 0x72, 0x9f, 0xe5,
		0x14, 0xd0, 0x4d, 0xe2, 0x00, 0x60, 0xa0, 0xe1, 0x06, 0x40, 0xa0, 0xe1,
		0x01, 0x30, 0xd4, 0xe4, 0x01, 0x00, 0x53, 0xe3, 0x1e, 0x00, 0x00, 0x0a,
		0x02, 0x00, 0x53, 0xe3, 0x45, 0x00, 0x00, 0x0a, 0x03, 0x00, 0x53, 0xe3,
		0x48, 0x00, 0x00, 0x0a, 0x04, 0x00, 0x53, 0xe3, 0x4c, 0x00, 0x00, 0x0a,
		0x05, 0x00, 0x53, 0xe3, 0x51, 0x00, 0x00, 0x0a, 0x06, 0x00, 0x53, 0xe3,
		0x60, 0x00, 0x00, 0x0a, 0x07, 0x00, 0x53, 0xe3, 0x6f, 0x00, 0x00, 0x0a,
		0x08, 0x00, 0x53, 0xe3, 0x7c, 0x00, 0x00, 0x1a, 0x0d, 0x90, 0xa0, 0xe1,
		0x08, 0x60, 0x8d, 0xe2, 0xb0, 0x80, 0xcd, 0xe1, 0x02, 0x10, 0xa0, 0xe3,
		0x09, 0x00, 0xa0, 0xe1, 0xb0, 0xff, 0xff, 0xeb, 0x01, 0x10, 0xa0, 0xe3,
		0x06, 0x00, 0xa0, 0xe1, 0x73, 0xff, 0xff, 0xeb, 0x08, 0x30, 0xdd, 0xe5,
		0x01, 0x00, 0x13, 0xe3, 0xf6, 0xff, 0xff, 0x1a, 0x04, 0x60, 0xa0, 0xe1,
		0x06, 0x40, 0xa0, 0xe1, 0x01, 0x30, 0xd4, 0xe4, 0x01, 0x00, 0x53, 0xe3,
		0xe0, 0xff, 0xff, 0x1a, 0x48, 0x38, 0x97, 0xe5, 0xb8, 0x21, 0x9f, 0xe5,
		0x0f, 0x30, 0xc3, 0xe3, 0x02, 0x30, 0x83, 0xe3, 0x48, 0x38, 0x87, 0xe5,
		0x48, 0x38, 0x97, 0xe5, 0xf0, 0x30, 0xc3, 0xe3, 0x20, 0x30, 0x83, 0xe3,
		0x48, 0x38, 0x87, 0xe5, 0x48, 0x38, 0x97, 0xe5, 0x0f, 0x3c, 0xc3, 0xe3,
		0x02, 0x3c, 0x83, 0xe3, 0x48, 0x38, 0x87, 0xe5, 0x48, 0x38, 0x97, 0xe5,
		0x0f, 0x3a, 0xc3, 0xe3, 0x02, 0x3a, 0x83, 0xe3, 0x48, 0x38, 0x87, 0xe5,
		0xc0, 0x32, 0x97, 0xe5, 0x01, 0x36, 0x83, 0xe3, 0xc0, 0x32, 0x87, 0xe5,
		0x60, 0x30, 0x97, 0xe5, 0x01, 0x36, 0x83, 0xe3, 0x60, 0x30, 0x87, 0xe5,
		0x24, 0x20, 0x85, 0xe5, 0x04, 0x30, 0x95, 0xe5, 0x02, 0x31, 0x83, 0xe3,
		0x83, 0x30, 0x83, 0xe3, 0x04, 0x30, 0x85, 0xe5, 0x04, 0x30, 0x95, 0xe5,
		0x00, 0x00, 0x53, 0xe3, 0xfc, 0xff, 0xff, 0xba, 0x08, 0x30, 0x95, 0xe5,
		0x04, 0x60, 0xa0, 0xe1, 0x03, 0x30, 0xc3, 0xe3, 0x44, 0x30, 0x83, 0xe3,
		0x08, 0x30, 0x85, 0xe5, 0x18, 0x30, 0x95, 0xe5, 0x02, 0x31, 0x83, 0xe3,
		0x02, 0x39, 0x83, 0xe3, 0x18, 0x30, 0x85, 0xe5, 0xb3, 0xff, 0xff, 0xea,
		0x08, 0x30, 0x95, 0xe5, 0x04, 0x60, 0xa0, 0xe1, 0xb0, 0x30, 0xc3, 0xe3,
		0x08, 0x30, 0x85, 0xe5, 0xae, 0xff, 0xff, 0xea, 0x08, 0x30, 0x95, 0xe5,
		0x04, 0x60, 0xa0, 0xe1, 0xb0, 0x30, 0xc3, 0xe3, 0x80, 0x30, 0x83, 0xe3,
		0x08, 0x30, 0x85, 0xe5, 0xa8, 0xff, 0xff, 0xea, 0x01, 0x90, 0xd6, 0xe5,
		0x02, 0x00, 0x86, 0xe2, 0x09, 0x10, 0xa0, 0xe1, 0x01, 0x60, 0x89, 0xe2,
		0x6c, 0xff, 0xff, 0xeb, 0x06, 0x60, 0x84, 0xe0, 0xa1, 0xff, 0xff, 0xea,
		0x05, 0x20, 0xd6, 0xe5, 0x06, 0x90, 0xd6, 0xe5, 0x01, 0x30, 0xd6, 0xe5,
		0x02, 0x40, 0xd6, 0xe5, 0x07, 0xe0, 0xd6, 0xe5, 0x03, 0xc0, 0xd6, 0xe5,
		0x08, 0x10, 0xd6, 0xe5, 0x04, 0x00, 0xd6, 0xe5, 0x09, 0x24, 0x82, 0xe1,
		0x04, 0x34, 0x83, 0xe1, 0x0e, 0x28, 0x82, 0xe1, 0x0c, 0x38, 0x83, 0xe1,
		0x01, 0x1c, 0x82, 0xe1, 0x00, 0x0c, 0x83, 0xe1, 0x5b, 0xff, 0xff, 0xeb,
		0x09, 0x60, 0x86, 0xe2, 0x90, 0xff, 0xff, 0xea, 0x05, 0x20, 0xd6, 0xe5,
		0x06, 0x90, 0xd6, 0xe5, 0x01, 0x30, 0xd6, 0xe5, 0x02, 0x40, 0xd6, 0xe5,
		0x07, 0xe0, 0xd6, 0xe5, 0x03, 0xc0, 0xd6, 0xe5, 0x08, 0x10, 0xd6, 0xe5,
		0x04, 0x00, 0xd6, 0xe5, 0x09, 0x24, 0x82, 0xe1, 0x04, 0x34, 0x83, 0xe1,
		0x0e, 0x28, 0x82, 0xe1, 0x0c, 0x38, 0x83, 0xe1, 0x01, 0x1c, 0x82, 0xe1,
		0x00, 0x0c, 0x83, 0xe1, 0x10, 0xff, 0xff, 0xeb, 0x09, 0x60, 0x86, 0xe2,
		0x7f, 0xff, 0xff, 0xea, 0x05, 0x30, 0xa0, 0xe3, 0x0d, 0x90, 0xa0, 0xe1,
		0x08, 0x60, 0x8d, 0xe2, 0x00, 0x30, 0xcd, 0xe5, 0x01, 0x10, 0xa0, 0xe3,
		0x09, 0x00, 0xa0, 0xe1, 0x41, 0xff, 0xff, 0xeb, 0x01, 0x10, 0xa0, 0xe3,
		0x06, 0x00, 0xa0, 0xe1, 0x04, 0xff, 0xff, 0xeb, 0x08, 0x30, 0xdd, 0xe5,
		0x01, 0x00, 0x13, 0xe3, 0xf6, 0xff, 0xff, 0x1a, 0x04, 0x60, 0xa0, 0xe1,
		0x8f, 0xff, 0xff, 0xea, 0x14, 0xd0, 0x8d, 0xe2, 0xf0, 0x83, 0xbd, 0xe8,
		0x0f, 0xc0, 0xff, 0xff, 0x00, 0x50, 0xc0, 0x01, 0x00, 0x00, 0xc2, 0x01,
		0x01, 0x10, 0x00, 0x00	
    };
    if(!sdram_initialized)
    {
        chip_ddr(ctx, "");                                                              // Init sdram required, the payload was modified to use buffer in SDRAM
    }
    
	fel_write(ctx, 0x00008800, (void *)&payload[0], sizeof(payload));                   // 0x8800 is the payload address
    
	if(swapbuf)
		*swapbuf = SDRAM_DATABUF;
	if(swaplen)
		*swaplen = SDRAM_DATABUF_SZ;
	if(cmdlen)
		*cmdlen = SDRAM_CMDBUF_SZ;
	return 1;
}

static int chip_spi_run(struct xfel_ctx_t * ctx, uint8_t * cbuf, uint32_t clen)
{
	fel_write(ctx, SDRAM_CMDBUF, (void *)cbuf, clen);                                   // Write SPI cmd buf into SDRAM buffer
	fel_exec(ctx, 0x00008800);                                                          // Execute SPI payload (Previously loaded to 0x8800)
	return 1;
}

struct chip_t f1c100s_f1c200s_f1c500s = {
	.name = "F1C100S/F1C200S/F1C500S",
	.detect = chip_detect,
	.reset = chip_reset,
	.sid = chip_sid,
	.jtag = chip_jtag,
	.ddr = chip_ddr,
	.spi_init = chip_spi_init,
	.spi_run = chip_spi_run,
};
