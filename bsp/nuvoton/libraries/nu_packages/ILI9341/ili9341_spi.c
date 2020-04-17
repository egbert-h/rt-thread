/**************************************************************************//**
*
* @copyright (C) 2020 Nuvoton Technology Corp. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*   1. Redistributions of source code must retain the above copyright notice,
*      this list of conditions and the following disclaimer.
*   2. Redistributions in binary form must reproduce the above copyright notice,
*      this list of conditions and the following disclaimer in the documentation
*      and/or other materials provided with the distribution.
*   3. Neither the name of Nuvoton Technology Corp. nor the names of its contributors
*      may be used to endorse or promote products derived from this software
*      without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Change Logs:
* Date            Author       Notes
* 2020-1-16       Wayne        First version
*
******************************************************************************/

#include <rtconfig.h>

#if defined(NU_PKG_USING_ILI9341_SPI)

#include <rtdevice.h>
#include <lcd_ili9341.h>

static struct rt_spi_device ili9341_spi_device;
static struct rt_spi_configuration ili9341_cfg =
{
    .mode = RT_SPI_MODE_0 | RT_SPI_MSB,
    .data_width = 8,
    .max_hz = 48000000,
};

static void ili9341_change_datawidth(int data_width)
{
    if (ili9341_cfg.data_width != data_width)
    {
        ili9341_cfg.data_width = data_width;
        rt_spi_configure(&ili9341_spi_device, &ili9341_cfg);
    }
}

void ili9341_send_cmd(rt_uint8_t cmd)
{
    ili9341_change_datawidth(8);
    CLR_RS;
    rt_spi_transfer(&ili9341_spi_device, (const void *)&cmd, NULL, 1);
    SET_RS;
}

void ili9341_send_cmd_parameter(uint8_t data)
{
    ili9341_change_datawidth(8);
    rt_spi_transfer(&ili9341_spi_device, (const void *)&data, NULL, 1);
}

static void ili9341_write_data_16bit(uint16_t data)
{
    ili9341_change_datawidth(16);
    rt_spi_transfer(&ili9341_spi_device, (const void *)&data, NULL, 2);
}

void ili9341_send_pixel_data(rt_uint16_t color)
{
    ili9341_write_data_16bit(color);
}

static rt_err_t ili9341_spi_send_then_recv(struct rt_spi_device *device,
        const void           *send_buf,
        rt_size_t             send_length,
        void                 *recv_buf,
        rt_size_t             recv_length)
{
    rt_err_t result;
    struct rt_spi_message message;

    RT_ASSERT(device != RT_NULL);
    RT_ASSERT(device->bus != RT_NULL);

    ili9341_change_datawidth(8);
    result = rt_mutex_take(&(device->bus->lock), RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        if (device->bus->owner != device)
        {
            /* not the same owner as current, re-configure SPI bus */
            result = device->bus->ops->configure(device, &device->config);
            if (result == RT_EOK)
            {
                /* set SPI bus owner */
                device->bus->owner = device;
            }
            else
            {
                /* configure SPI bus failed */
                result = -RT_EIO;
                goto __exit;
            }
        }

        /* send data */
        message.send_buf   = send_buf;
        message.recv_buf   = RT_NULL;
        message.length     = send_length;
        message.cs_take    = 1;
        message.cs_release = 0;
        message.next       = RT_NULL;

        CLR_RS;
        result = device->bus->ops->xfer(device, &message);
        SET_RS;
        if (result == 0)
        {
            result = -RT_EIO;
            goto __exit;
        }

        /* recv data */
        message.send_buf   = RT_NULL;
        message.recv_buf   = recv_buf;
        message.length     = recv_length;
        message.cs_take    = 0;
        message.cs_release = 1;
        message.next       = RT_NULL;

        result = device->bus->ops->xfer(device, &message);
        if (result == 0)
        {
            result = -RT_EIO;
            goto __exit;
        }

        result = RT_EOK;
    }
    else
    {
        return -RT_EIO;
    }

__exit:

    rt_mutex_release(&(device->bus->lock));
    return result;
}

void ili9341_set_column(uint16_t StartCol, uint16_t EndCol)
{
    ili9341_send_cmd(0x2A);
    ili9341_write_data_16bit(StartCol);
    ili9341_write_data_16bit(EndCol);
}

void ili9341_set_page(uint16_t StartPage, uint16_t EndPage)
{
    ili9341_send_cmd(0x2B);
    ili9341_write_data_16bit(StartPage);
    ili9341_write_data_16bit(EndPage);
}

void ili9341_lcd_get_pixel(char *color, int x, int y)
{
    uint8_t cmd;
    typedef union
    {
        rt_uint32_t rgbx;
        struct
        {
            rt_uint8_t x;
            rt_uint8_t r;
            rt_uint8_t g;
            rt_uint8_t b;
        } S;
    } ili9341_pixel;
    ili9341_pixel bgrx;

    if (x >= XSIZE_PHYS || y >= YSIZE_PHYS)
    {
        *(rt_uint16_t *)color = 0;
        return;
    }

    ili9341_set_column(x, x);
    ili9341_set_page(y, y);

    cmd  = 0x2E;
    ili9341_spi_send_then_recv(&ili9341_spi_device, &cmd, 1, &bgrx, 4);
    //rt_kprintf("%08x.\n", bgrx);

    // To RGB565
    *(rt_uint16_t *)color = ((bgrx.S.r >> 3) << 11) | ((bgrx.S.g >> 2) << 5) | (bgrx.S.b >> 3);
}

rt_err_t rt_hw_lcd_ili9341_spi_init(const char *spibusname)
{
    if (rt_spi_bus_attach_device(&ili9341_spi_device, "lcd_ili9341", spibusname, RT_NULL) != RT_EOK)
        return -RT_ERROR;

    return rt_spi_configure(&ili9341_spi_device, &ili9341_cfg);
}

#endif /* #if defined(NU_PKG_USING_ILI9341_SPI) */
