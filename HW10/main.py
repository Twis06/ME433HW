import glob
import os
import time

import pgzrun
from pgzero.rect import Rect
from random import choice, randint, random


WIDTH = 800
HEIGHT = 600
TITLE = "Pico Road Racer"

ROAD_LEFT = 180
ROAD_RIGHT = 620
ROAD_WIDTH = ROAD_RIGHT - ROAD_LEFT
LANES = [250, 350, 450, 550]
PLAYER_Y = 500
IMU_DEADZONE = 0.08
IMU_FULL_TILT_G = 0.75
PICO_STEER_SPEED = 420
MAX_ENERGY = 100
CLEAR_ENERGY_COST = 40
CLEAR_DURATION = 1.0
STAR_ENERGY = 25
SERIAL_PORT_PATTERNS = (
    "/dev/tty.usbmodem*",
    "/dev/ttyACM*",
    "/dev/cu.usbmodem*",
)

player = Rect((0, 0), (46, 78))
player.center = (400, PLAYER_Y)

traffic = []
stars = []
road_offset = 0
speed = 4.0
score = 0
energy = 40
game_over = False
spawn_timer = 0
star_timer = 0
flash_timer = 0
clear_timer = 0
clear_button_was_down = False


class SerialPicoController:
    def __init__(self):
        self.available = False
        self.status = "keyboard fallback"
        self.steering = 0.0
        self.accelerating = False
        self.buffer = b""
        self.fd = None
        self.last_scan = 0.0
        self.neutral_y = None
        self.calibration_samples = 0

        self._connect()

    def _connect(self):
        self.last_scan = time.monotonic()
        for path in self._candidate_ports():
            try:
                self.fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
                self.available = True
                self.status = f"serial: {os.path.basename(path)}"
                self.neutral_y = None
                self.calibration_samples = 0
                return
            except OSError:
                continue

        self.available = False
        self.status = "keyboard fallback"

    def _candidate_ports(self):
        configured = os.environ.get("PICO_SERIAL_PORT")
        if configured:
            return [configured]

        ports = []
        for pattern in SERIAL_PORT_PATTERNS:
            ports.extend(glob.glob(pattern))
        return sorted(ports)

    def read(self):
        if not self.available:
            if time.monotonic() - self.last_scan > 1.0:
                self._connect()
            return self.steering, self.accelerating

        try:
            while True:
                chunk = os.read(self.fd, 128)
                if not chunk:
                    break
                self.buffer += chunk
        except BlockingIOError:
            pass
        except OSError:
            self.available = False
            self.status = "Pico serial lost"
            return 0.0, False

        if b"\n" not in self.buffer:
            return self.steering, self.accelerating

        lines = self.buffer.splitlines()
        self.buffer = b"" if self.buffer.endswith(b"\n") else lines.pop()
        for line in reversed(lines):
            if self._parse_line(line):
                break

        return self.steering, self.accelerating

    def _parse_line(self, line):
        try:
            text = line.decode("utf-8").strip()
            parts = text.replace("y=", "").replace("button=", "").split(",")
            accel_y = float(parts[0])
            button = int(parts[1])
        except (IndexError, ValueError, UnicodeDecodeError):
            return False

        if self.neutral_y is None:
            self.neutral_y = accel_y
            self.calibration_samples = 1
            self.status = "calibrating center"
            return True
        if self.calibration_samples < 30:
            self.neutral_y += (accel_y - self.neutral_y) / (self.calibration_samples + 1)
            self.calibration_samples += 1
            self.status = "calibrating center"
            return True

        if self.status == "calibrating center":
            self.status = "Pico USB active"

        steering =  max(-1.0, min(1.0, (accel_y - self.neutral_y) / IMU_FULL_TILT_G))
        if abs(steering) < IMU_DEADZONE:
            steering = 0.0
        self.steering = self.steering * 0.65 + steering * 0.35
        self.accelerating = button == 1
        return True

    def recalibrate(self):
        self.neutral_y = None
        self.calibration_samples = 0
        self.steering = 0.0
        self.status = "calibrating center"


def make_pico_controller():
    serial_controller = SerialPicoController()
    if serial_controller.available:
        return serial_controller

    return serial_controller


pico_controller = make_pico_controller()


def reset_game():
    global traffic, stars, road_offset, speed, score, energy, game_over
    global spawn_timer, star_timer, flash_timer, clear_timer, clear_button_was_down
    player.center = (400, PLAYER_Y)
    traffic = []
    stars = []
    road_offset = 0
    speed = 4.0
    score = 0
    energy = 40
    game_over = False
    spawn_timer = 0
    star_timer = 0.8
    flash_timer = 0
    clear_timer = 0
    clear_button_was_down = False


def draw_car(rect, body_color, window_color, stripe_color=None):
    screen.draw.filled_rect(rect, body_color)
    screen.draw.rect(rect, "black")

    hood = Rect((rect.x + 8, rect.y + 8), (rect.w - 16, 18))
    cabin = Rect((rect.x + 8, rect.y + 30), (rect.w - 16, 26))
    bumper = Rect((rect.x + 7, rect.bottom - 12), (rect.w - 14, 6))
    screen.draw.filled_rect(hood, window_color)
    screen.draw.filled_rect(cabin, window_color)
    screen.draw.filled_rect(bumper, "black")

    if stripe_color:
        stripe = Rect((rect.centerx - 4, rect.y + 6), (8, rect.h - 12))
        screen.draw.filled_rect(stripe, stripe_color)


def draw_star(rect):
    cx, cy = rect.center
    screen.draw.filled_circle((cx, cy), 11, "gold")
    screen.draw.filled_circle((cx, cy), 5, "white")
    screen.draw.line((cx, cy - 15), (cx, cy + 15), "gold")
    screen.draw.line((cx - 15, cy), (cx + 15, cy), "gold")
    screen.draw.line((cx - 10, cy - 10), (cx + 10, cy + 10), "gold")
    screen.draw.line((cx - 10, cy + 10), (cx + 10, cy - 10), "gold")


def spawn_traffic():
    lane = choice(LANES)
    width = randint(42, 54)
    height = randint(66, 86)
    rect = Rect((0, -height), (width, height))
    rect.centerx = lane

    for car in traffic:
        if abs(car["rect"].centerx - rect.centerx) < 8 and car["rect"].y < 140:
            return

    traffic.append(
        {
            "rect": rect,
            "color": choice(["firebrick", "orange", "dodgerblue", "mediumseagreen", "plum"]),
            "speed": speed + randint(1, 4) + random(),
        }
    )


def spawn_star():
    lane = choice(LANES)
    rect = Rect((0, -24), (24, 24))
    rect.centerx = lane

    for car in traffic:
        if abs(car["rect"].centerx - rect.centerx) < 18 and car["rect"].y < 160:
            return

    stars.append({"rect": rect, "speed": speed + 1.2})


def handle_input(dt):
    global energy, flash_timer, clear_timer, clear_button_was_down
    steering = 340 * dt
    pico_steering, pico_button_down = pico_controller.read()

    if keyboard.a or keyboard.left:
        player.x -= steering
    if keyboard.d or keyboard.right:
        player.x += steering
    if pico_steering:
        player.x += pico_steering * PICO_STEER_SPEED * dt
    if keyboard.w or keyboard.up:
        player.y -= steering * 0.65
    if keyboard.s or keyboard.down:
        player.y += steering * 0.65

    if keyboard.c:
        pico_controller.recalibrate()

    clear_pressed = keyboard.space or pico_button_down
    clear_started = clear_pressed and not clear_button_was_down
    clear_button_was_down = clear_pressed
    if clear_started and energy >= CLEAR_ENERGY_COST and clear_timer <= 0:
        energy -= CLEAR_ENERGY_COST
        clear_timer = CLEAR_DURATION
        flash_timer = 0.08

    player.left = max(ROAD_LEFT + 12, player.left)
    player.right = min(ROAD_RIGHT - 12, player.right)
    player.top = max(40, player.top)
    player.bottom = min(HEIGHT - 16, player.bottom)

    return clear_timer > 0


def update(dt):
    global road_offset, speed, score, energy, game_over
    global spawn_timer, star_timer, flash_timer, clear_timer

    if game_over:
        if keyboard.r:
            reset_game()
        return

    clearing = handle_input(dt)
    current_speed = speed

    road_offset = (road_offset + current_speed * 90 * dt) % 80
    speed += 0.08 * dt
    score += current_speed * 8 * dt
    flash_timer = max(0, flash_timer - dt)
    clear_timer = max(0, clear_timer - dt)

    spawn_timer -= dt
    if spawn_timer <= 0:
        spawn_traffic()
        spawn_timer = max(0.35, 1.0 - speed * 0.06)

    star_timer -= dt
    if star_timer <= 0:
        spawn_star()
        star_timer = 1.8 + random() * 1.8

    for car in traffic:
        car["rect"].y += car["speed"] * 60 * dt
    for star in stars:
        star["rect"].y += star["speed"] * 60 * dt

    traffic[:] = [car for car in traffic if car["rect"].top < HEIGHT + 40]
    stars[:] = [star for star in stars if star["rect"].top < HEIGHT + 40]

    for star in stars[:]:
        if player.colliderect(star["rect"]):
            stars.remove(star)
            energy = min(MAX_ENERGY, energy + STAR_ENERGY)

    if clearing:
        traffic.clear()
        return

    for car in traffic:
        if player.colliderect(car["rect"]):
            game_over = True
            break


def draw_road():
    screen.fill((30, 132, 74))
    screen.draw.filled_rect(Rect((ROAD_LEFT, 0), (ROAD_WIDTH, HEIGHT)), (42, 45, 48))
    screen.draw.line((ROAD_LEFT, 0), (ROAD_LEFT, HEIGHT), "white")
    screen.draw.line((ROAD_RIGHT, 0), (ROAD_RIGHT, HEIGHT), "white")

    for lane_x in [300, 400, 500]:
        y = -80 + road_offset
        while y < HEIGHT:
            screen.draw.filled_rect(Rect((lane_x - 4, y), (8, 44)), "gold")
            y += 80

    for y in range(0, HEIGHT, 80):
        screen.draw.filled_circle((130, y + 22), 12, (24, 100, 52))
        screen.draw.filled_circle((670, y + 58), 12, (24, 100, 52))


def draw_hud():
    screen.draw.text(f"Score: {int(score)}", (18, 16), fontsize=34, color="white", shadow=(1, 1))
    screen.draw.text("Tilt IMU: left/right", (18, 54), fontsize=20, color="white", shadow=(1, 1))
    screen.draw.text("GP15 / SPACE: clear", (18, 78), fontsize=20, color="white", shadow=(1, 1))
    screen.draw.text("C: recalibrate", (18, 102), fontsize=18, color="white", shadow=(1, 1))

    meter = Rect((628, 20), (150, 18))
    fill = Rect((628, 20), (150 * energy / MAX_ENERGY, 18))
    screen.draw.rect(meter, "white")
    screen.draw.filled_rect(fill, "gold")
    screen.draw.text(f"ENERGY {int(energy)}", (628, 43), fontsize=22, color="white", shadow=(1, 1))
    if clear_timer > 0:
        screen.draw.text("ROAD CLEAR", center=(400, 112), fontsize=36, color="gold", shadow=(1, 1))


def draw_instructions_overlay():
    panel = Rect((18, 462), (146, 118))
    screen.draw.filled_rect(panel, (18, 20, 24))
    screen.draw.rect(panel, "white")
    screen.draw.text("PICO", (32, 472), fontsize=20, color="gold")
    screen.draw.text("GP18 SDA", (32, 496), fontsize=16, color="white")
    screen.draw.text("GP19 SCL", (32, 516), fontsize=16, color="white")
    screen.draw.text("GP15 clear", (32, 536), fontsize=16, color="white")
    screen.draw.text("Stars +energy", (32, 556), fontsize=16, color="gold")
    screen.draw.text(pico_controller.status, (32, 572), fontsize=12, color="deepskyblue")


def draw_game_over():
    panel = Rect((210, 205), (380, 170))
    screen.draw.filled_rect(panel, (18, 20, 24))
    screen.draw.rect(panel, "white")
    screen.draw.text("CRASH!", center=(400, 245), fontsize=58, color="tomato")
    screen.draw.text(f"Final score: {int(score)}", center=(400, 302), fontsize=34, color="white")
    screen.draw.text("Press R to restart", center=(400, 342), fontsize=28, color="white")


def draw():
    draw_road()

    if flash_timer > 0:
        screen.draw.filled_rect(Rect((ROAD_LEFT, 0), (ROAD_WIDTH, HEIGHT)), (55, 70, 95))

    for star in stars:
        draw_star(star["rect"])

    for car in traffic:
        draw_car(car["rect"], car["color"], "lightcyan")

    draw_car(player, "red", "skyblue", "white")
    draw_hud()
    draw_instructions_overlay()

    if game_over:
        draw_game_over()


reset_game()
pgzrun.go()
