/*
	Библиотека работы с силовыми выходами платы переднего и заднего освещения.

	@Dragon_Knight https://github.com/Dragon-Knight, 2023.
*/

#pragma once

#include <inttypes.h>

extern ADC_HandleTypeDef hadc1;

template <uint8_t _ports_max> 
class PowerOut
{
	using event_short_circuit_t = void (*)(uint8_t num, uint16_t current);
	
	enum mode_t : uint8_t { MODE_OFF, MODE_ON, MODE_PWM, MODE_BLINK };

	typedef struct
	{
		GPIO_TypeDef *port;
		uint16_t pin_digital;
		uint16_t pin_analog;
		
		mode_t mode;
		GPIO_PinState state;
		uint16_t blink_on;
		uint16_t blink_off;
		uint32_t blink_time;
		uint32_t blink_delay;
	} channel_t;
	
	public:
		
		PowerOut(uint32_t vref, uint8_t gain, uint8_t shunt) : _vref(vref), _gain(gain), _shunt(shunt)
		{
			memset(&_channels, 0x00, sizeof(_channels));
			
			return;
		}

		void Init()
		{
			HAL_ADCEx_Calibration_Start(&hadc1);
			
			return;
		}
		
		void AddPort(channel_t port)
		{
			if(_ports_idx == _ports_max) return;
			
			_channels[_ports_idx++] = port;
			
			return;
		}
		
		void RegShortCircuitEvent(event_short_circuit_t event)
		{
			_event_short_circuit = event;
			
			return;
		}
		
		bool SetOn(uint8_t out)
		{
			if(out == 0 || out > _ports_max) return false;
			
			channel_t &channel = _channels[out-1];
			
			_HW_HIGH(channel);
			channel.mode = MODE_ON;
			_delayTick(10000);
			
			uint16_t current = _HW_GetCurrent(channel);
			if( _CheckCurrent(current) == 1 )
			{
				_HW_LOW(channel);
				
				return false;
			}
			
			return true;
		}
		
		bool SetOn(uint8_t out, uint16_t blink_on, uint16_t blink_off)
		{
			if(out == 0 || out > _ports_max) return false;
			
			if( SetOn(out) == true )
			{
				channel_t &channel = _channels[out-1];
				
				channel.blink_on = blink_on;
				channel.blink_off = blink_off;
				channel.mode = MODE_BLINK;

				return true;
			}
			
			return false;
		}
		
		void SetOff(uint8_t out)
		{
			if(out == 0 || out > _ports_max) return;
			
			channel_t &channel = _channels[out-1];
			
			_HW_LOW(channel);
			channel.mode = MODE_OFF;
			
			return;
		}
		
		uint16_t GetCurrent(uint8_t out)
		{
			if(out == 0 || out > _ports_max) return 0;

			channel_t &channel = _channels[out-1];
			
			return _HW_GetCurrent(channel);
		}
		
		void Processing(uint32_t current_time)
		{
			if(current_time - _last_tick_time < 10) return;
			_last_tick_time = current_time;
			
			uint16_t current;
			for(uint8_t i = 0; i < _ports_max; ++i)
			{
				channel_t &channel = _channels[i];
				
				if(channel.port == NULL) continue;
				if(channel.mode == MODE_OFF) continue;
				
				current = _HW_GetCurrent(channel);
				if( _CheckCurrent(current) == 1 )
				{
					_HW_LOW(channel);
					
					if(_event_short_circuit != nullptr)
					{
						_event_short_circuit( (i + 1), current );
					}
				}
				
				// TODO: При выполеннии SetOn(out, blink_on, blink_off) выход включается, сразу выключается и начинает моргать.
				if(channel.mode == MODE_BLINK && current_time - channel.blink_time > channel.blink_delay)
				{
					channel.blink_time = current_time;
					
					if(channel.state == GPIO_PIN_RESET)
					{
						channel.blink_delay = channel.blink_on;
						_HW_HIGH(channel);
					}
					else
					{
						channel.blink_delay = channel.blink_off;
						_HW_LOW(channel);
					}
				}
			}
			
			return;
		}
		
	private:
		
		void _HW_HIGH(channel_t &channel)
		{
			HAL_GPIO_WritePin(channel.port, channel.pin_digital, GPIO_PIN_SET);
			channel.state = GPIO_PIN_SET;
			
			return;
		}
		
		void _HW_LOW(channel_t &channel)
		{
			HAL_GPIO_WritePin(channel.port, channel.pin_digital, GPIO_PIN_RESET);
			channel.state = GPIO_PIN_RESET;
			
			return;
		}
		
		uint16_t _HW_GetCurrent(channel_t &channel)
		{
			_adc_config.Channel = channel.pin_analog;
			
			HAL_ADC_ConfigChannel(&hadc1, &_adc_config);
			//HAL_ADCEx_Calibration_Start(&hadc1);
			HAL_ADC_Start(&hadc1);
			HAL_ADC_PollForConversion(&hadc1, 5);
			uint16_t adc = HAL_ADC_GetValue(&hadc1);
			//HAL_ADC_Stop(&hadc1);
			
			return ((((_vref / 4095) * adc) / _gain) / _shunt);
		}
		
		int8_t _CheckCurrent(uint16_t current)
		{
			if(current < 50) return -1;
			else if(current > 5000) return 1;
			else return 0;
		}
		
		void _delayTick(uint16_t nop_tick)
		{
			while(--nop_tick) { asm("NOP"); }
			
			return;
		}
		
		const uint32_t _vref;
		const uint8_t _gain;
		const uint8_t _shunt;
		
		channel_t _channels[_ports_max];
		uint8_t _ports_idx = 0;
		
		ADC_ChannelConfTypeDef _adc_config = { ADC_CHANNEL_1, ADC_REGULAR_RANK_1, ADC_SAMPLETIME_1CYCLE_5 };
		
		event_short_circuit_t _event_short_circuit = nullptr;
		
		uint32_t _last_tick_time = 0;
};
