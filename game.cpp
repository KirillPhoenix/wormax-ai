#include <SFML/Graphics.hpp>
#include <deque>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <unordered_set>
#include <ctime> // Для srand(time(nullptr))

namespace GameConfig {
    constexpr float arenaRadius = 2500.f;
    constexpr int initialLength = 30;
    const sf::Vector2f arenaCenter{arenaRadius, arenaRadius};
    constexpr float wormRadius = 6.f;
    constexpr int foodCount = 15;
    constexpr int botCount = 20;
}

float distance(sf::Vector2f a, sf::Vector2f b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

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
    float radius = GameConfig::wormRadius;
    float scaleRadius = 1.f;
    float maxScaleRadius = 2.5f;
    int growthLeft = 0;

    sf::Vector2f direction = {1.f, 0.f};
    float maxTurnRate = 15.0f;

    bool isGhost = false;
    bool isBoosting = false;
    bool isStopped = false;

    sf::Clock ghostTimer;
    sf::Clock ghostCooldownTimer;
    sf::Clock boostDrainTimer;

    float ghostDuration = 2.f;
    float ghostCooldownTime = 10.f;
    bool ghostCooldown = false;
    float boostDrainInterval = 1.f;

    bool stopCooldown = false;
    float stopCooldownTime = 3.f;
    sf::Clock stopCooldownTimer;

    Worm(sf::Vector2f startPos, int length) {
        growthLeft += length;
        segments.push_back(startPos);
    }

    void setBoosting(bool boosting) {
        isBoosting = boosting;
        currentSpeed = (boosting && canBoost()) ? fastSpeed : normalSpeed;
    }

    void setStopped(bool value) {
        if (value && !stopCooldown && canStop()) {
            isStopped = true;
        } else if (!value && isStopped) {
            isStopped = false;
            stopCooldown = true;
            stopCooldownTimer.restart();
        }
    }

    bool canGhost() const { return segments.size() >= 500; }
    bool canStop() const { return segments.size() >= 100; }
    bool canBoost() const { return segments.size() > 10; }

    void activateGhost() {
        if (ghostCooldown || !canGhost()) return;
        isGhost = true;
        ghostTimer.restart();
        ghostCooldown = true;
        ghostCooldownTimer.restart();
    }

    void updateBonuses() {
        if (isGhost && ghostTimer.getElapsedTime().asSeconds() >= ghostDuration)
            isGhost = false;

        if (ghostCooldown && ghostCooldownTimer.getElapsedTime().asSeconds() >= ghostCooldownTime)
            ghostCooldown = false;

        if (stopCooldown && stopCooldownTimer.getElapsedTime().asSeconds() >= stopCooldownTime)
            stopCooldown = false;
    }

    void updateDirection(sf::Vector2f target, float deltaTime) {
        sf::Vector2f head = getHead();
        sf::Vector2f toTarget = normalize(target - head);
        float angle = angleBetween(direction, toTarget);
        float maxAngle = maxTurnRate * deltaTime;

        if (angle <= maxAngle) {
            direction = toTarget;
            return;
        }

        float sign = cross(direction, toTarget) < 0 ? -1.f : 1.f;
        float rotateAngle = sign * maxAngle;

        float sinA = std::sin(rotateAngle);
        float cosA = std::cos(rotateAngle);

        direction = normalize({
            direction.x * cosA - direction.y * sinA,
            direction.x * sinA + direction.y * cosA
        });
    }

    void moveForward(float deltaTime) {
        updateBonuses();
        if (isStopped) return;

        sf::Vector2f head = segments.front();
        sf::Vector2f newHead = head + direction * currentSpeed * deltaTime;

        if (distance(newHead, GameConfig::arenaCenter) > GameConfig::arenaRadius) {
            *this = Worm(GameConfig::arenaCenter, GameConfig::initialLength);
            return;
        }

        segments.push_front(newHead);
        if (growthLeft > 0) --growthLeft;
        else if (segments.size() > 10) segments.pop_back();

        if (isBoosting && canBoost() && boostDrainTimer.getElapsedTime().asSeconds() >= boostDrainInterval) {
            boostDrainTimer.restart();
            if (growthLeft > 0) --growthLeft;
            else if (segments.size() > 10) segments.pop_back();
        }
    }

    void grow() {
        growthLeft += 10;
        if (scaleRadius < maxScaleRadius)
            scaleRadius += 0.05f;
    }

    virtual void render(sf::RenderWindow& window, sf::Color color) {
        float visualRadius = radius * scaleRadius;
        sf::Color c = isGhost ? sf::Color(color.r, color.g, color.b, 100) : color;

        for (auto& pos : segments) {
            sf::CircleShape circle(visualRadius);
            circle.setOrigin(visualRadius, visualRadius);
            circle.setPosition(pos);
            circle.setFillColor(c);
            window.draw(circle);
        }
    }

    sf::Vector2f getHead() const { return segments.front(); }
    float getScaledRadius() const { return radius * scaleRadius; }

    bool checkCollisionWith(const Worm& other) const {
        if (isGhost || other.isGhost) return false;
        for (size_t i = 1; i < other.segments.size(); ++i) {
            if (distance(getHead(), other.segments[i]) < getScaledRadius() + other.getScaledRadius())
                return true;
        }
        return false;
    }

    bool checkHeadOnCollision(const Worm& other) const {
        if (isGhost || other.isGhost) return false;
        return distance(getHead(), other.getHead()) < getScaledRadius() + other.getScaledRadius();
    }
};

class BotWorm : public Worm {
public:
    float directionTimer = 0.f;
    float directionInterval = 5.0f;
    float botTurnRate = 6.0f;
    sf::Color color = sf::Color(rand() % 256, rand() % 256, rand() % 256);

    BotWorm(sf::Vector2f startPos, int length) : Worm(startPos, length) {
        randomizeDirection();
    }

    void avoidObstacles(const std::vector<BotWorm>& bots, const Worm& player) {
        const float avoidDistance = 40.f;
        const float turnStrength = 4.0f;

        sf::Vector2f avoidanceVector{0.f, 0.f};

        for (size_t i = 0; i < player.segments.size(); ++i) {
            float d = distance(getHead(), player.segments[i]);
            if (d < avoidDistance && d > 0.01f) {
                float weight = (avoidDistance - d) / avoidDistance;
                avoidanceVector += normalize(getHead() - player.segments[i]) * weight / d;
            }
        }

        for (const auto& bot : bots) {
            if (&bot == this) continue;
            for (size_t i = 0; i < bot.segments.size(); ++i) {
                float d = distance(getHead(), bot.segments[i]);
                if (d < avoidDistance && d > 0.01f) {
                    float weight = (avoidDistance - d) / avoidDistance;
                    avoidanceVector += normalize(getHead() - bot.segments[i]) * weight / d;
                }
            }
        }

        if (length(avoidanceVector) > 0.01f) {
            sf::Vector2f newDir = normalize(direction + avoidanceVector * turnStrength);
            direction = newDir;
        }
    }

    void update(float deltaTime, const sf::Vector2u& winSize, const std::vector<BotWorm>& bots, const Worm& player) {
        directionTimer += deltaTime;
        if (directionTimer >= directionInterval) {
            directionTimer = 0.f;
            sf::Vector2f projected = getHead() + direction * 50.f;
            bool nearObstacle = false;
            for (const auto& bot : bots) {
                if (&bot == this) continue;
                if (distance(projected, bot.getHead()) < 40.f) {
                    nearObstacle = true;
                    break;
                }
            }
            if (distance(projected, player.getHead()) < 40.f) nearObstacle = true;
            if (!nearObstacle) randomizeDirection();
        }

        avoidObstacles(bots, player);

        float oldRate = maxTurnRate;
        maxTurnRate = botTurnRate;
        updateDirection(getHead() + direction * 100.f, deltaTime);
        maxTurnRate = oldRate;

        moveForward(deltaTime);
    }

    void render(sf::RenderWindow& window, sf::Color ignore = sf::Color::Red) override {
        Worm::render(window, color);
    }

private:
    void randomizeDirection() {
        float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
        direction = {std::cos(angle), std::sin(angle)};
    }
};

class Food {
public:
    sf::Vector2f position;
    float size = 10.f;
    sf::Color color = sf::Color(rand() % 256, rand() % 256, rand() % 256);

    Food() { respawn(); }

    void respawn() {
        float angle = (rand() / (float)RAND_MAX) * 2 * 3.14159f;
        float radius = std::sqrt(rand() / (float)RAND_MAX) * GameConfig::arenaRadius;
        position = GameConfig::arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
        color = sf::Color(rand() % 256, rand() % 256, rand() % 256);
    }

    void render(sf::RenderWindow& window) {
        sf::CircleShape circle(size / 2);
        circle.setFillColor(color);
        circle.setOrigin(size / 2, size / 2);
        circle.setPosition(position);
        window.draw(circle);
    }

    bool isEatenBy(const Worm& worm) {
        return distance(worm.getHead(), position) < worm.getScaledRadius() + size / 2;
    }
};

void spawnFoodFromWorm(const Worm& worm, std::vector<Food>& foods) {
    for (const auto& segment : worm.segments) {
        Food f;
        f.position = segment;
        f.size = 6.f + static_cast<float>(rand() % 5);
        foods.push_back(f);
    }
}

int main() {
    // Инициализация генератора случайных чисел
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    sf::RenderWindow window(sf::VideoMode(800, 600), "Wormax Mini with Bots");
    window.setFramerateLimit(60);

    float spawnAngle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
    float spawnRadius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * GameConfig::arenaRadius;
    sf::Vector2f spawnPos = GameConfig::arenaCenter + sf::Vector2f(std::cos(spawnAngle), std::sin(spawnAngle)) * spawnRadius;
    Worm player(spawnPos, GameConfig::initialLength);

    sf::View view({0, 0, 400, 300});
    view.setCenter(GameConfig::arenaCenter);
    window.setView(view);

    std::vector<Food> foods(GameConfig::foodCount);
    std::vector<BotWorm> bots;

    for (int i = 0; i < GameConfig::botCount; ++i) {
        float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
        float radius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * GameConfig::arenaRadius;
        sf::Vector2f pos = GameConfig::arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
        bots.emplace_back(pos, 20);
    }

    sf::Clock clock;

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event))
            if (event.type == sf::Event::Closed) window.close();

        float deltaTime = clock.restart().asSeconds();

        player.setBoosting(sf::Keyboard::isKeyPressed(sf::Keyboard::Q));
        player.setStopped(sf::Keyboard::isKeyPressed(sf::Keyboard::W));
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::E))
            player.activateGhost();

        player.updateDirection(window.mapPixelToCoords(sf::Mouse::getPosition(window)), deltaTime);
        player.moveForward(deltaTime);

        std::vector<Worm*> allWorms = {&player};
        std::unordered_set<Worm*> deadWorms;
        for (auto& bot : bots) allWorms.push_back(&bot);

        for (size_t i = 0; i < allWorms.size(); ++i) {
            for (size_t j = 0; j < allWorms.size(); ++j) {
                if (i == j) continue;
                Worm* a = allWorms[i];
                Worm* b = allWorms[j];
                if (a->checkCollisionWith(*b)) deadWorms.insert(a);
                else if (a->checkHeadOnCollision(*b)) {
                    int lenA = a->segments.size();
                    int lenB = b->segments.size();
                    if (lenA < lenB) deadWorms.insert(a);
                    else if (lenB < lenA) deadWorms.insert(b);
                    else { deadWorms.insert(a); deadWorms.insert(b); }
                }
            }
        }

        // Исправленный респавн ботов
        std::vector<BotWorm> newBots;
        for (auto* worm : deadWorms) {
            spawnFoodFromWorm(*worm, foods);
            if (worm == &player) {
                float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
                float radius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * GameConfig::arenaRadius;
                sf::Vector2f pos = GameConfig::arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
                player = Worm(pos, GameConfig::initialLength);
            } else {
                // Добавляем нового бота в newBots
                float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
                float radius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * GameConfig::arenaRadius;
                sf::Vector2f pos = GameConfig::arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
                newBots.emplace_back(pos, 20);
                //std::cout << "Bot respawned at (" << pos.x << ", " << pos.y << ")\n"; // Отладка
            }
        }

        // Обновляем список ботов
        for (auto& bot : bots) {
            if (deadWorms.find(&bot) == deadWorms.end()) {
                newBots.push_back(std::move(bot)); // Переносим живых ботов
            }
        }
        bots = std::move(newBots); // Заменяем старый список новым

        for (auto& bot : bots) bot.update(deltaTime, window.getSize(), bots, player);

        for (auto& food : foods) {
            if (food.isEatenBy(player)) {
                player.grow();
                food.respawn();
            }
        }

        view.setCenter(player.getHead());
        window.setView(view);

        window.clear(sf::Color::Black);

        sf::CircleShape arenaBorder(GameConfig::arenaRadius);
        arenaBorder.setOrigin(GameConfig::arenaRadius, GameConfig::arenaRadius);
        arenaBorder.setPosition(GameConfig::arenaCenter);
        arenaBorder.setFillColor(sf::Color(30, 30, 30));
        arenaBorder.setOutlineColor(sf::Color(100, 100, 100));
        arenaBorder.setOutlineThickness(-6);
        window.draw(arenaBorder);

        for (auto& food : foods) food.render(window);
        for (auto& bot : bots) bot.render(window);
        player.render(window, sf::Color::Green);

        window.display();
    }

    return 0;
}