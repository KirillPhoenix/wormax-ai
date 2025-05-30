#include <SFML/Graphics.hpp>
#include <deque>
#include <cmath>
#include <cstdlib>
#include <iostream>

const float ARENA_RADIUS = 2500.f;
const sf::Vector2f ARENA_CENTER(ARENA_RADIUS, ARENA_RADIUS);

float distance(sf::Vector2f a, sf::Vector2f b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Старая normalize
// sf::Vector2f normalize(sf::Vector2f v) {
//     float len = std::hypot(v.x, v.y);
//     return len != 0 ? v / len : sf::Vector2f(0, 0);
// }

float length(sf::Vector2f v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

sf::Vector2f normalize(sf::Vector2f v) {
    float len = length(v);
    return len == 0 ? sf::Vector2f(0, 0) : v / len;
}

float dot(sf::Vector2f a, sf::Vector2f b) {
    return a.x * b.x + a.y * b.y;
}

float cross(sf::Vector2f a, sf::Vector2f b) {
    return a.x * b.y - a.y * b.x;
}

float angleBetween(sf::Vector2f a, sf::Vector2f b) {
    a = normalize(a);
    b = normalize(b);
    float dotVal = std::clamp(dot(a, b), -1.f, 1.f);
    return std::acos(dotVal);
}



class Worm {
public:
    std::deque<sf::Vector2f> segments;
    float segmentSpacing = 10.f;
    float normalSpeed = 250.f;
    float fastSpeed = 500.f;
    float currentSpeed = normalSpeed;
    float radius = 6.f;
    int growthLeft = 0;

    sf::Vector2f direction = {1.f, 0.f}; // начальное направление — вправо
    float maxTurnRate = 10.14f; // радиан/сек — можно настраивать

    Worm(sf::Vector2f startPos) {
        segments.push_back(startPos);
    }

    void setBoosting(bool boosting) {
        currentSpeed = boosting ? fastSpeed : normalSpeed;
    }

    void updateDirection(sf::Vector2f target, float deltaTime) {
        sf::Vector2f head = segments.front();
        sf::Vector2f toTarget = normalize(target - head);

        float angle = angleBetween(direction, toTarget);
        float maxAngle = maxTurnRate * deltaTime;

        // если можем довернуть полностью, просто установить направление
        if (angle <= maxAngle) {
            direction = toTarget;
            return;
        }

        // вычисляем угол поворота
        float sign = cross(direction, toTarget) < 0 ? -1.f : 1.f;
        float rotateAngle = sign * maxAngle;

        float sinA = std::sin(rotateAngle);
        float cosA = std::cos(rotateAngle);

        sf::Vector2f newDir = {
            direction.x * cosA - direction.y * sinA,
            direction.x * sinA + direction.y * cosA
        };

        direction = normalize(newDir);
    }


    void moveForward(float deltaTime) {
        sf::Vector2f head = segments.front();
        sf::Vector2f newHead = head + direction * currentSpeed * deltaTime;

        float distFromCenter = distance(newHead, ARENA_CENTER);
        if (distFromCenter > ARENA_RADIUS) {
            *this = Worm(ARENA_CENTER);
            return;
        }

        segments.push_front(newHead);
        if (growthLeft > 0)
            --growthLeft;
        else
            segments.pop_back();
    }

    void grow() {
        growthLeft += 10;
    }

    void render(sf::RenderWindow& window, sf::Color color) {
        for (auto& pos : segments) {
            sf::CircleShape circle(radius);
            circle.setOrigin(radius, radius);
            circle.setPosition(pos);
            circle.setFillColor(color);
            window.draw(circle);
        }
    }

    sf::Vector2f getHead() const {
        return segments.front();
    }

    float getRadius() const {
        return radius;
    }
};

class BotWorm : public Worm {
public:
    //sf::Vector2f direction;
    float directionTimer = 0.f;
    float directionInterval = 5.0f; // менять направление каждые 2 секунды

    BotWorm(sf::Vector2f startPos) : Worm(startPos) {
        randomizeDirection();

        int length = rand() % 10 + 5; // от 5 до 14 сегментов
        for (int i = 0; i < length; ++i)
            grow();
    }


    void update(float deltaTime, const sf::Vector2u& windowSize) {
        directionTimer += deltaTime;
        if (directionTimer >= directionInterval) {
            directionTimer = 0.f;
            randomizeDirection(); // меняет direction напрямую
        }

        // Обновляем направление чуть-чуть в сторону точки (чтобы не было рывка)
        sf::Vector2f newTarget = segments.front() + direction * 500.f;

        // Проверка границ
        float dist = distance(newTarget, ARENA_CENTER);
        if (dist > ARENA_RADIUS) {
            randomizeDirection();
        }

        // Бот всегда идёт вперёд (по направлению)
        moveForward(deltaTime);
    }

private:
    void randomizeDirection() {
        float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
        direction = sf::Vector2f(std::cos(angle), std::sin(angle));
    }
};

class Food {
public:
    sf::Vector2f position;
    float size = 10.f;

    Food(sf::Vector2u windowSize) {
        respawn();
    }

    void respawn() {
        float angle = (rand() / (float)RAND_MAX) * 2 * 3.14159f;
        float radius = std::sqrt(rand() / (float)RAND_MAX) * ARENA_RADIUS;

        position = {
            ARENA_CENTER.x + std::cos(angle) * radius,
            ARENA_CENTER.y + std::sin(angle) * radius
        };
    }

    void render(sf::RenderWindow& window) {
        sf::CircleShape circle(size / 2);
        circle.setFillColor(sf::Color::Red);
        circle.setOrigin(size / 2, size / 2);
        circle.setPosition(position);
        window.draw(circle);
    }

    bool isEatenBy(const Worm& worm) {
        return distance(worm.getHead(), position) < worm.getRadius() + size / 2;
    }
};

int main() {
    sf::RenderWindow window(sf::VideoMode(800, 600), "Wormax Mini with Bots");
    window.setFramerateLimit(60);

    Worm player(ARENA_CENTER);
    for (int i = 0; i < 10; ++i)
        player.grow();

    sf::View view(sf::FloatRect(0, 0, window.getSize().x, window.getSize().y));
    view.setSize(800.f / 2.f, 600.f / 2.f); // Приближаем в 2 раза
    view.setCenter(ARENA_CENTER); // Начальный центр — центр арены
    window.setView(view);

    const int foodCount = 15;
    std::vector<Food> foods;
    for (int i = 0; i < foodCount; ++i)
        foods.emplace_back(window.getSize());

    const int botCount = 10;
    std::vector<BotWorm> bots;
    for (int i = 0; i < botCount; ++i) {
        float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
        float radius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * ARENA_RADIUS;
        sf::Vector2f pos = {
            ARENA_CENTER.x + std::cos(angle) * radius,
            ARENA_CENTER.y + std::sin(angle) * radius
        };
        bots.emplace_back(pos);
    }

    sf::Clock clock;
    sf::Vector2f smoothedPlayerPos = player.getHead(); // Для сглаживания позиции игрока

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event))
            if (event.type == sf::Event::Closed)
                window.close();

        float deltaTime = clock.restart().asSeconds();

        bool boosting = sf::Mouse::isButtonPressed(sf::Mouse::Left);
        player.setBoosting(boosting);

        sf::Vector2f mouseWorldPos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
        player.updateDirection(mouseWorldPos, deltaTime);
        player.moveForward(deltaTime);

        // Обновление ботов
        for (auto& bot : bots)
            bot.update(deltaTime, window.getSize());

        // Еда — проверка на поедание
        for (auto& food : foods) {
            if (food.isEatenBy(player)) {
                player.grow();
                food.respawn();
            }
        }

        // Плавное слежение за игроком
        sf::Vector2f playerPos = player.getHead();
        sf::Vector2f viewCenter = view.getCenter();

        // Интерполяция центра камеры в сторону игрока
        float followSpeed = 5.0f; // чем выше — тем быстрее камера следует
        sf::Vector2f toPlayer = playerPos - viewCenter;
        viewCenter += toPlayer * followSpeed * deltaTime;

        // Ограничение — не вылезать за границы арены
        float maxDistance = ARENA_RADIUS - view.getSize().x / 2.f;
        float distanceFromCenter = distance(viewCenter, ARENA_CENTER);

        if (distanceFromCenter > maxDistance) {
            viewCenter = ARENA_CENTER + normalize(viewCenter - ARENA_CENTER) * maxDistance;
        }

        view.setCenter(viewCenter);

        window.setView(view);

        window.clear(sf::Color::Black);

        // Отрисовка арены (фон)
        sf::CircleShape arenaBorder(ARENA_RADIUS);
        arenaBorder.setOrigin(ARENA_RADIUS, ARENA_RADIUS);
        arenaBorder.setPosition(ARENA_CENTER);
        arenaBorder.setFillColor(sf::Color(30, 30, 30));
        arenaBorder.setOutlineColor(sf::Color(100, 100, 100));
        arenaBorder.setOutlineThickness(10);
        window.draw(arenaBorder);

        // Еда
        for (auto& food : foods)
            food.render(window);

        // Боты
        for (auto& bot : bots)
            bot.render(window, sf::Color::Red);

        // Игрок
        player.render(window, sf::Color::Green);

        window.display();
    }

    return 0;
}