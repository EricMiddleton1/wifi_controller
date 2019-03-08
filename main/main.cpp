//
// async_udp_echo_server.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2018 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <iostream>
#include <future>

#include "asio.hpp"

extern "C" {
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"

  #include "driver/gpio.h"
  #include "driver/adc.h"
}

#define ADC_LX  ADC1_CHANNEL_7   //GPIO33
#define ADC_LY  ADC1_CHANNEL_6   //GPIO32
#define ADC_RX  ADC1_CHANNEL_5   //GPIO35
#define ADC_RY  ADC1_CHANNEL_4   //GPIO34

using asio::ip::udp;

class server
{
public:
  server(asio::io_context& io_context, short recvPort, short sendPort)
    : socket_(io_context, udp::endpoint(udp::v4(), recvPort))
    , m_sendPort{sendPort}
    , m_bound{false}
  {
    do_receive();
  }

  void do_receive()
  {
    socket_.async_receive_from(
        asio::buffer(data_, max_length), sender_endpoint_,
        [this](std::error_code ec, std::size_t bytes_recvd)
        {
          if (!ec && bytes_recvd > 0)
          {
            std::cout << "[Info] Received broadcast datagram" << std::endl;

            if(!m_bound) {
              m_boundEndpoint = sender_endpoint_;
              m_boundEndpoint.port(m_sendPort);
              m_bound = true;

              std::cout << "[Info] Binding to broadcast endpoint" << std::endl;
            }
          }
          
          do_receive();
        });
  }

  template<typename DataType>
  void send(const DataType& data )
  {
    socket_.async_send_to(
        asio::buffer(data), m_boundEndpoint,
        [this](std::error_code ec, std::size_t  bytes /*bytes_sent*/)
        {
          if(ec) {
            std::cout << "[Error] Failed to send: " << ec.message() << std::endl;
          }
        });
  }

  bool isBound() const {
    return m_bound;
  }

  udp::endpoint& boundEndpoint() {
    return m_boundEndpoint;
  }

private:
  udp::socket socket_;
  udp::endpoint sender_endpoint_;
  udp::endpoint m_boundEndpoint;
  short m_sendPort;
  bool m_bound;
  enum { max_length = 1024 };
  char data_[max_length];
};

void joystick_task(void* arg) {
  auto& io_context = *reinterpret_cast<asio::io_context*>(arg);

  server racecarServer{io_context, 1235, 1234};

  adc1_config_width(ADC_WIDTH_BIT_12);
  
  //Configure attenuation 1/3.6 (full range for ADC 0-1.1v)
  adc1_config_channel_atten(ADC_LX, ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC_LY, ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC_RX, ADC_ATTEN_DB_11);
  adc1_config_channel_atten(ADC_RY, ADC_ATTEN_DB_11);

  std::array<int8_t, 2> speeds;
  
  while(true) {
    int lx, ly, rx, ry;

    lx = 4095 - adc1_get_raw(ADC_LX);
    ly = adc1_get_raw(ADC_LY);
    rx = adc1_get_raw(ADC_RX);
    ry = 4095 - adc1_get_raw(ADC_RY);

    speeds[0] = static_cast<int8_t>((ly-2048) * 100 / 2048);
    speeds[1] = static_cast<int8_t>((ry-2048) * 100 / 2048);

    //printf("%d, %d\n", (int)speeds[0], (int)speeds[1]);
    
    if(racecarServer.isBound()) {
      racecarServer.send(speeds);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void asio_main()
{
    asio::io_context io_context;

  printf("Creating joystick task\n");

  xTaskCreate(joystick_task, "joystick", 4096, &io_context, 5, NULL);

  for(;;) {
    io_context.run();
    io_context.restart();

    std::cout << "[Warning] io_context.run() return, restarting" << std::endl;
  }

  printf("[Error] Exiting main!\n");

}
