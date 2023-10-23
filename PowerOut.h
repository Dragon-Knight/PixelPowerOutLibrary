/*
	Библиотека работы с силовыми выходами платы переднего и заднего освещения.

	@Dragon_Knight https://github.com/Dragon-Knight, 2023.
*/

#pragma once

#include <inttypes.h>

extern ADC_HandleTypeDef hadc1;

template <uint8_t _ports_max, uint16_t _tick_time = 10> 
class PowerOut
{
	using event_short_circuit_t = void (*)(uint8_t num, uint16_t current);
	
	enum mode_t : uint8_t { MODE_OFF, MODE_ON, MODE_PWM, MODE_BLINK, MODE_DELAY_OFF };
	
	public:
		
		typedef struct
		{
			GPIO_TypeDef *Port;
			uint16_t Pin;
		} pin_t;
		
		PowerOut(uint32_t vref, uint8_t gain, uint8_t shunt) : _vref(vref), _gain(gain), _shunt(shunt)
		{
			memset(_channels, 0x00, sizeof(_channels));
			
			return;
		}

		void Init()
		{
			HAL_ADCEx_Calibration_Start(&hadc1);
			
			return;
		}
		
		void AddPort(pin_t digital, pin_t analog, uint16_t current_limit)
		{
			if(_ports_idx == _ports_max) return;
			
			channel_t &channel = _channels[_ports_idx++];
			channel.pin_digital = digital;
			channel.pin_analog = analog;
			channel.current_limit = current_limit;
			
			_HW_PinInit(channel.pin_digital, GPIO_MODE_OUTPUT_PP);
			
			#warning This line stoped TIM1 and some periphery...
			//_HW_PinInit(channel.pin_analog, GPIO_MODE_ANALOG);
			
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
			_delayTick(5000);
			
			_HW_GetCurrent(channel);
			if( _CheckCurrent(channel) == 1 )
			{
				_HW_LOW(channel);
				
				return false;
			}
			
			return true;
		}
		
		// Включить на указанное время.
		bool SetOn(uint8_t out, uint32_t delay)
		{
			if(out == 0 || out > _ports_max) return false;
			
			if( SetOn(out) == true )
			{
				SetOff(out, delay);

				return true;
			}
			
			return false;
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
				
				// Исправляем 'промигивание' при включении. Костыли, но как иначе? :'(
				channel.blink_delay = blink_on;
				channel.init_time = HAL_GetTick();
				
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
			channel.current = 0;
			
			return;
		}
		
		// Выключить через указанное время в мс.
		void SetOff(uint8_t out, uint32_t delay)
		{
			if(out == 0 || out > _ports_max) return;
			
			channel_t &channel = _channels[out-1];
			channel.mode = MODE_DELAY_OFF;
			channel.off_delay = delay;
			channel.init_time = HAL_GetTick();
			
			return;
		}
		
		void SetToggle(uint8_t out)
		{
			if(out == 0 || out > _ports_max) return;
			
			channel_t &channel = _channels[out-1];
			switch(channel.mode)
			{
				case MODE_BLINK:
				case MODE_ON:
				case MODE_PWM:
				{
					SetOff(out);
					break;
				}
				case MODE_DELAY_OFF:
				case MODE_OFF:
				{
					SetOn(out);
					break;
				}
				default:
				{
					break;
				}
			}

			return;
		}
		
		uint16_t GetCurrent(uint8_t out)
		{
			if(out == 0 || out > _ports_max) return 0;

			channel_t &channel = _channels[out-1];
			
			return channel.current;
		}

		uint16_t GetCurrentTotal()
		{
			uint16_t result = 0;
			
			for(channel_t &channel : _channels)
			{
				result += channel.current;
			}
			
			return result;
		}

		mode_t GetState(uint8_t out)
		{
			if(out == 0 || out > _ports_max) return (mode_t)0;
			
			return _channels[out-1].mode;
		}
		
		void Processing(uint32_t current_time)
		{
			if(current_time - _last_tick_time < _tick_time) return;
			_last_tick_time = current_time;
			
			for(uint8_t i = 0; i < _ports_max; ++i)
			{
				channel_t &channel = _channels[i];
				
				if(channel.current_limit == 0) continue;
				if(channel.mode == MODE_OFF) continue;
				
				_HW_GetCurrent(channel);
				if( _CheckCurrent(channel) == 1 )
				{
					_HW_LOW(channel);
					
					if(_event_short_circuit != nullptr)
					{
						_event_short_circuit( (i + 1), channel.current );
					}
				}
				
				if(channel.mode == MODE_BLINK && current_time - channel.init_time > channel.blink_delay)
				{
					channel.init_time = current_time;
					
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

				if(channel.mode == MODE_DELAY_OFF && current_time - channel.init_time > channel.off_delay)
				{
					SetOff( (i + 1) );
				}
			}
			
			return;
		}
		
	private:

		typedef struct
		{
			pin_t pin_digital;
			pin_t pin_analog;
			uint16_t current_limit;

			mode_t mode;
			GPIO_PinState state;
			uint16_t blink_on;
			uint16_t blink_off;
			uint32_t blink_delay;
			uint32_t off_delay;
			uint32_t init_time;
			
			uint16_t current;
		} channel_t;
		
		void _HW_HIGH(channel_t &channel)
		{
			HAL_GPIO_WritePin(channel.pin_digital.Port, channel.pin_digital.Pin, GPIO_PIN_SET);
			channel.state = GPIO_PIN_SET;
			
			return;
		}
		
		void _HW_LOW(channel_t &channel)
		{
			HAL_GPIO_WritePin(channel.pin_digital.Port, channel.pin_digital.Pin, GPIO_PIN_RESET);
			channel.state = GPIO_PIN_RESET;
			
			return;
		}
		
		void _HW_GetCurrent(channel_t &channel)
		{
			_adc_config.Channel = channel.pin_analog.Pin;
			
			HAL_ADC_ConfigChannel(&hadc1, &_adc_config);
			//HAL_ADCEx_Calibration_Start(&hadc1);
			HAL_ADC_Start(&hadc1);
			HAL_ADC_PollForConversion(&hadc1, 5);
			uint16_t adc = HAL_ADC_GetValue(&hadc1);
			//HAL_ADC_Stop(&hadc1);
			
			channel.current = ((((_vref / 4095) * adc) / _gain) / _shunt);
			
			return;
		}
		
		void _HW_PinInit(pin_t pin, uint32_t mode)
		{
			HAL_GPIO_WritePin(pin.Port, pin.Pin, GPIO_PIN_RESET);
			
			_pin_config.Pin = pin.Pin;
			_pin_config.Mode = mode;
			HAL_GPIO_Init(pin.Port, &_pin_config);
			
			return;
		}
		
		int8_t _CheckCurrent(channel_t &channel)
		{
			if(channel.current < 50) return -1;
			else if(channel.current > channel.current_limit) return 1;
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
		
		GPIO_InitTypeDef _pin_config = { GPIO_PIN_0, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW };
		ADC_ChannelConfTypeDef _adc_config = { ADC_CHANNEL_0, ADC_REGULAR_RANK_1, ADC_SAMPLETIME_1CYCLE_5 };
		
		event_short_circuit_t _event_short_circuit = nullptr;
		
		uint32_t _last_tick_time = 0;
};
