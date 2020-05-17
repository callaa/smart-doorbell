class Bell {
public:
	Bell(int pin)
		: last_ring(0), remaining_rings(0), pin(pin)
	{
	}

	void step() {
		if(remaining_rings > 0) {
			if((unsigned long)(millis() - last_ring) > RING_INTERVAL) {
				digitalWrite(pin, HIGH);
				delay(PULSE_TIME);
				digitalWrite(pin, LOW);

				remaining_rings -= 1;
				last_ring = millis();
			}
		}
	}

	void ring(int times) {
		last_ring = 0;
		remaining_rings = times;
	}

private:
	static const unsigned long PULSE_TIME = 200;
	static const unsigned long RING_INTERVAL = 300;
	unsigned long last_ring;
	int remaining_rings;
	int pin;
};

