/*
	Библиотека работы с силовыми выходами платы переднего и заднего освещения.

	@Dragon_Knight https://github.com/Dragon-Knight, 2023.
*/

#pragma once
#include <inttypes.h>

#if defined(STM32H7)
	#define ADC_SAMPLETIME_7CYCLES_5 ADC_SAMPLETIME_8CYCLES_5
#endif

template <uint8_t _ports_max, uint16_t _tick_time = 10> 
class PowerOut
{
	using event_short_circuit_t = void (*)(uint8_t num, uint16_t current);
	using event_external_control_t = void (*)(uint8_t external_id, GPIO_PinState state);
	
	enum mode_t : uint8_t { MODE_OFF, MODE_ON, MODE_PWM, MODE_BLINK, MODE_DELAY_OFF };
	
	public:
		
		typedef struct
		{
			GPIO_TypeDef *Port;
			uint16_t Pin;
			uint32_t Channel;
		} a_pin_t;
		
		typedef struct
		{
			GPIO_TypeDef *Port;
			uint16_t Pin;
		} d_pin_t;
		
		PowerOut(ADC_HandleTypeDef *hadc, uint32_t vref, uint8_t gain, uint8_t shunt) : _hadc(hadc), _vref(vref), _gain(gain), _shunt(shunt)
		{
			memset(_channels, 0x00, sizeof(_channels));
			
			return;
		}

		void Init()
		{
			HAL_ADC_Start(_hadc);
			
			return;
		}
		
		// Добавить порт, где d_pin_t и a_pin_t являются нативными пинами контроллера
		void AddPort(d_pin_t digital, a_pin_t analog, uint16_t current_limit)
		{
			if(_ports_idx == _ports_max) return;

			channel_t &channel = _channels[_ports_idx++];
			channel.pin_digital = digital;
			channel.pin_analog = analog;
			channel.current_limit = current_limit;

			_HW_PinInitD(channel.pin_digital);
			_HW_PinInitA(channel.pin_analog);

			return;
		}
		
		// Добавить порт, где пин управления управляется через колбек
		// uint8_t external_id - некий ID который поможет идентифицировать порт в колбеке
		void AddPort(uint8_t external_id, a_pin_t analog, uint16_t current_limit)
		{
			if(_ports_idx == _ports_max) return;
			
			channel_t &channel = _channels[_ports_idx++];
			channel.pin_digital_external_id = external_id;
			channel.pin_analog = analog;
			channel.current_limit = current_limit;
			
			_HW_PinInitA(channel.pin_analog);
			
			return;
		}

		void RegExternalControlEvent(event_external_control_t event)
		{
			_event_external_control = event;

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
			_delayTick(1000);
			
			if(channel.current_limit == 0) return true;
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
		
		bool SetOff(uint8_t out)
		{
			if(out == 0 || out > _ports_max) return false;
			
			channel_t &channel = _channels[out-1];
			
			_HW_LOW(channel);
			channel.mode = MODE_OFF;
			channel.current = 0;
			
			return true;
		}
		
		// Выключить через указанное время в мс.
		bool SetOff(uint8_t out, uint32_t delay)
		{
			if(out == 0 || out > _ports_max) return false;
			
			channel_t &channel = _channels[out-1];
			channel.mode = MODE_DELAY_OFF;
			channel.off_delay = delay;
			channel.init_time = HAL_GetTick();
			
			return true;
		}
		
		bool SetToggle(uint8_t out)
		{
			if(out == 0 || out > _ports_max) return false;
			
			bool result = false;
			channel_t &channel = _channels[out-1];
			switch(channel.mode)
			{
				case MODE_BLINK:
				case MODE_ON:
				case MODE_PWM:
				{
					result = SetOff(out);
					break;
				}
				case MODE_DELAY_OFF:
				case MODE_OFF:
				{
					result = SetOn(out);
					break;
				}
				default:
				{
					break;
				}
			}

			return result;
		}
		
		// Выставить указанное состояние
		bool SetWrite(uint8_t out, uint8_t state)
		{
			if(out == 0 || out > _ports_max) return false;
			
			if(state == 0)
				return SetOff(out);
			else
				return SetOn(out);
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
			
			if(--_calibration_countdown == 0)
			{
				_calibration_countdown = _calibration_delay_tick;
				
				_HW_Calibration();
			}
			
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
			uint8_t pin_digital_external_id;
			d_pin_t pin_digital;
			a_pin_t pin_analog;
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
			if(channel.pin_digital.Pin == 0){
				if(_event_external_control)
					_event_external_control(channel.pin_digital_external_id, GPIO_PIN_SET);
			} else {
				HAL_GPIO_WritePin(channel.pin_digital.Port, channel.pin_digital.Pin, GPIO_PIN_SET);
			}
			channel.state = GPIO_PIN_SET;
			
			return;
		}
		
		void _HW_LOW(channel_t &channel)
		{
			if(channel.pin_digital.Pin == 0){
				if(_event_external_control)
					_event_external_control(channel.pin_digital_external_id, GPIO_PIN_RESET);
			} else {
				HAL_GPIO_WritePin(channel.pin_digital.Port, channel.pin_digital.Pin, GPIO_PIN_RESET);
			}
			channel.state = GPIO_PIN_RESET;
			
			return;
		}
		
		void _HW_GetCurrent(channel_t &channel)
		{
			_adc_config.Channel = channel.pin_analog.Channel;
			
			HAL_ADC_ConfigChannel(_hadc, &_adc_config);
			HAL_ADC_Start(_hadc);
			HAL_ADC_PollForConversion(_hadc, 5);
			uint16_t adc = HAL_ADC_GetValue(_hadc);
			
			channel.current = ((((_vref / 4095) * adc) / _gain) / _shunt);
			
			return;
		}
		
		void _HW_PinInitD(d_pin_t pin)
		{
			if(pin.Pin == 0) return;
			
			HAL_GPIO_WritePin(pin.Port, pin.Pin, GPIO_PIN_RESET);
			
			_pin_config.Pin = pin.Pin;
			_pin_config.Mode = GPIO_MODE_OUTPUT_PP;
			HAL_GPIO_Init(pin.Port, &_pin_config);
			
			return;
		}
		
		void _HW_PinInitA(a_pin_t pin)
		{
			if(pin.Pin == 0) return;
			
			HAL_GPIO_WritePin(pin.Port, pin.Pin, GPIO_PIN_RESET);
			
			_pin_config.Pin = pin.Pin;
			_pin_config.Mode = GPIO_MODE_ANALOG;
			HAL_GPIO_Init(pin.Port, &_pin_config);
			
			return;
		}
		
		int8_t _CheckCurrent(channel_t &channel)
		{
			if(channel.current < 50) return -1;
			else if(channel.current > channel.current_limit) return 1;
			else return 0;
		}
		
		void _HW_Calibration()
		{
			HAL_ADC_Stop(_hadc);
#if defined(STM32F1)
			HAL_ADCEx_Calibration_Start(_hadc);
#elif defined(STM32H7)
			HAL_ADCEx_Calibration_Start(_hadc, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
#endif
			HAL_ADC_Start(_hadc);
			
			return;
		}
		
		void _delayTick(uint16_t nop_tick)
		{
			while(--nop_tick) { asm("nop;"); }
			
			return;
		}
		
		ADC_HandleTypeDef *_hadc;
		const uint32_t _vref;
		const uint8_t _gain;
		const uint8_t _shunt;
		
		channel_t _channels[_ports_max];
		uint8_t _ports_idx = 0;
		
		GPIO_InitTypeDef _pin_config = { GPIO_PIN_0, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW };
		ADC_ChannelConfTypeDef _adc_config = { ADC_CHANNEL_0, ADC_REGULAR_RANK_1, ADC_SAMPLETIME_7CYCLES_5 };
		
		event_short_circuit_t _event_short_circuit = nullptr;
		event_external_control_t _event_external_control = nullptr;
		
		uint32_t _last_tick_time = 0;
		uint32_t _calibration_countdown = 1;
		const uint32_t _calibration_delay_tick = (60000 / _tick_time);	// 60 sec
};
