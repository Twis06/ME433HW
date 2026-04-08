import board
import pwmio
import time

# there are other libraries for controlling RC servo motors
# but really all you need is PWM at 50Hz
servo = pwmio.PWMOut(board.GP16, variable_frequency=True)
servo.frequency = 50 # hz

while True:
    # pulse 0.5 ms to 2.5 ms out of a possible 20 ms (50Hz)
    # for 0 degrees to 180 degrees
    # so duty_cycle can be 65535*0.5/20 to 65535*2.5/20
    # but check this, some servo brands might only want 1-2 ms
    
    # command the servo to move from 0 to 180 degrees 
    
    min_pos = 65535*0.5/20 # 65535 is max for 16bit PWM. 20ms is for the servo, 0.5ms is the start position pulse command 
    max_pos = 65535*2.4/20 
    
    ######
    # move in one direction
    ######
    # for i in range(int(min_pos), int(max_pos), 100): ## 100 is stepsize
    #     servo.duty_cycle = i
    #     time.sleep(0.1)
        
        
    ######
    # loop
    ######
    pulse = int(min_pos) # need to cast to int 
    step = 100
    while True:
        servo.duty_cycle = int(pulse)
        pulse += step
        if pulse >= max_pos or pulse <= min_pos:
            step = -step
        time.sleep(0.02) # time for rest 