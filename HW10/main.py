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

player = Rect((0, 0), (46, 78))
player.center = (400, PLAYER_Y)

traffic = []
road_offset = 0
speed = 4.0
score = 0
boost = 100
game_over = False
spawn_timer = 0
flash_timer = 0


def reset_game():
    global traffic, road_offset, speed, score, boost, game_over, spawn_timer, flash_timer
    player.center = (400, PLAYER_Y)
    traffic = []
    road_offset = 0
    speed = 4.0
    score = 0
    boost = 100
    game_over = False
    spawn_timer = 0
    flash_timer = 0


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


def handle_input(dt):
    global boost, flash_timer
    steering = 340 * dt

    if keyboard.a or keyboard.left:
        player.x -= steering
    if keyboard.d or keyboard.right:
        player.x += steering
    if keyboard.w or keyboard.up:
        player.y -= steering * 0.65
    if keyboard.s or keyboard.down:
        player.y += steering * 0.65

    boosting = keyboard.space and boost > 0
    if boosting:
        boost = max(0, boost - 45 * dt)
        flash_timer = 0.08
    else:
        boost = min(100, boost + 18 * dt)

    player.left = max(ROAD_LEFT + 12, player.left)
    player.right = min(ROAD_RIGHT - 12, player.right)
    player.top = max(40, player.top)
    player.bottom = min(HEIGHT - 16, player.bottom)

    return boosting


def update(dt):
    global road_offset, speed, score, game_over, spawn_timer, flash_timer

    if game_over:
        if keyboard.r:
            reset_game()
        return

    boosting = handle_input(dt)
    current_speed = speed + (4.5 if boosting else 0)

    road_offset = (road_offset + current_speed * 90 * dt) % 80
    speed += 0.08 * dt
    score += current_speed * 8 * dt
    flash_timer = max(0, flash_timer - dt)

    spawn_timer -= dt
    if spawn_timer <= 0:
        spawn_traffic()
        spawn_timer = max(0.35, 1.0 - speed * 0.06)

    for car in traffic:
        car["rect"].y += car["speed"] * 60 * dt

    traffic[:] = [car for car in traffic if car["rect"].top < HEIGHT + 40]

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
    screen.draw.text("WASD drive  SPACE boost", (18, 54), fontsize=24, color="white", shadow=(1, 1))

    meter = Rect((610, 20), (160, 18))
    fill = Rect((610, 20), (160 * boost / 100, 18))
    screen.draw.rect(meter, "white")
    screen.draw.filled_rect(fill, "deepskyblue")
    screen.draw.text("BOOST", (610, 43), fontsize=22, color="white", shadow=(1, 1))


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

    for car in traffic:
        draw_car(car["rect"], car["color"], "lightcyan")

    draw_car(player, "red", "skyblue", "white")
    draw_hud()

    if game_over:
        draw_game_over()


reset_game()
pgzrun.go()
