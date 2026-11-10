#pragma once
// Stand-in for ESP-IDF 5.5's driver/spi_master.h, for tools/panel_bus_test.cpp: the types, flags and calls
// src/panel_bus.cpp uses (flag values as in ESP-IDF), implemented by the test as a model of the bus.
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
typedef uint32_t TickType_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define portMAX_DELAY 0xffffffffu

typedef enum { SPI1_HOST = 0, SPI2_HOST = 1, SPI3_HOST = 2 } spi_host_device_t;
typedef int spi_clock_source_t;
#define SPI_CLK_SRC_DEFAULT 0

#define SPI_DEVICE_HALFDUPLEX (1 << 4)
#define SPI_TRANS_MODE_DIO (1 << 0)
#define SPI_TRANS_MODE_QIO (1 << 1)
#define SPI_TRANS_USE_RXDATA (1 << 2)
#define SPI_TRANS_USE_TXDATA (1 << 3)
#define SPI_TRANS_MODE_DIOQIO_ADDR (1 << 4)
#define SPI_TRANS_VARIABLE_CMD (1 << 5)
#define SPI_TRANS_VARIABLE_ADDR (1 << 6)
#define SPI_TRANS_VARIABLE_DUMMY (1 << 7)
#define SPI_TRANS_CS_KEEP_ACTIVE (1 << 8)
#define SPI_TRANS_MULTILINE_CMD (1 << 9)
#define SPI_TRANS_MULTILINE_ADDR SPI_TRANS_MODE_DIOQIO_ADDR

struct spi_transaction_t {
    uint32_t flags;
    uint16_t cmd;
    uint64_t addr;
    size_t length;
    size_t rxlength;
    uint32_t override_freq_hz;
    void* user;
    union {
        const void* tx_buffer;
        uint8_t tx_data[4];
    };
    union {
        void* rx_buffer;
        uint8_t rx_data[4];
    };
};
typedef struct spi_transaction_t spi_transaction_t;

typedef struct {
    spi_transaction_t base;
    uint8_t command_bits, address_bits, dummy_bits;
} spi_transaction_ext_t;

typedef struct {
    uint8_t command_bits, address_bits, dummy_bits, mode;
    spi_clock_source_t clock_source;
    uint16_t duty_cycle_pos, cs_ena_pretrans;
    uint8_t cs_ena_posttrans;
    int clock_speed_hz, input_delay_ns, sample_point, spics_io_num;
    uint32_t flags;
    int queue_size;
    void* pre_cb;
    void* post_cb;
} spi_device_interface_config_t;

typedef struct spi_device_t* spi_device_handle_t;

esp_err_t spi_bus_add_device(spi_host_device_t, const spi_device_interface_config_t*, spi_device_handle_t*);
esp_err_t spi_bus_remove_device(spi_device_handle_t);
esp_err_t spi_bus_get_max_transaction_len(spi_host_device_t, size_t*);
esp_err_t spi_device_queue_trans(spi_device_handle_t, spi_transaction_t*, TickType_t);
esp_err_t spi_device_get_trans_result(spi_device_handle_t, spi_transaction_t**, TickType_t);
esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t*);
