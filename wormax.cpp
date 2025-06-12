#include <thread>
#include <SFML/Network.hpp>
#include <SFML/Graphics.hpp>
#include <deque>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <unordered_set>
#include <ctime>
#include <fstream>


float g_last_step_reward = 0.0f;
sf::TcpListener rewardListener;
sf::TcpSocket rewardSocket;
std::thread rewardThread;

void rewardServer() {
    rewardListener.listen(12345);
    rewardListener.accept(rewardSocket);
    while (true) {
        sf::Packet packet;
        packet << g_last_step_reward;
        rewardSocket.send(packet);
        std::this_thread::sleep_for(std::chrono::milliseconds(30)); // 30 FPS
    }
}

namespace GameConfig {
    constexpr float arenaRadius = 2500.f;
    constexpr int initialLength = 30;
    const sf::Vector2f arenaCenter{arenaRadius, arenaRadius};
    constexpr float wormRadius = 6.f;
    constexpr int foodCount = 10;
    constexpr int botCount = 30;
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

int randomInRange(int a, int b) {
    return a + rand() % (b - a + 1);
}

class Worm {
public:
    std::deque<sf::Vector2f> segments;
    float segmentSpacing = 10.f;
    float normalSpeed = 150.f;
    float fastSpeed = 300.f;
    float currentSpeed = normalSpeed;
    float radius = GameConfig::wormRadius;
    float scaleRadius = 1.f;
    float maxScaleRadius = 2.5f;
    int growthLeft = 0;
    static constexpr size_t max_length = 300; // Ограничение длины
    sf::Color color = sf::Color(rand() % 256, rand() % 256, rand() % 256);


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
    sf::Clock stopDurationTimer;

    bool isBot = false;
    std::string name = "";


    Worm(sf::Vector2f startPos, int length) {
        growthLeft += length;
        segments.push_back(startPos);
    }

    void setBoosting(bool boosting) {
        isBoosting = boosting;
        currentSpeed = (boosting && canBoost()) ? fastSpeed : normalSpeed;
    }

    void setStopped(bool keyPressed) {
        updateBonuses();
        if (keyPressed && !isStopped && !stopCooldown && canStop()) {
            isStopped = true;
            stopDurationTimer.restart();
        }

        if (isStopped) {
            bool expired = stopDurationTimer.getElapsedTime().asSeconds() >= 3.f;
            if (!keyPressed || expired) {
                isStopped = false;
                stopCooldown = true;
                stopCooldownTimer.restart();
            }
        }
    }

    bool canGhost() const { return segments.size() >= 200; }
    bool canStop() const { return segments.size() >= 100; }
    bool canBoost() const { return segments.size() > 10; }

    void activateGhost() {
        if (isGhost || ghostCooldown || !canGhost()) return;

        isGhost = true;
        ghostTimer.restart();
    }

    void updateBonuses() {
        if (isGhost && ghostTimer.getElapsedTime().asSeconds() >= ghostDuration) {
            isGhost = false;
            ghostCooldown = true;
            ghostCooldownTimer.restart();
        }

        if (ghostCooldown && ghostCooldownTimer.getElapsedTime().asSeconds() >= ghostCooldownTime) {
            ghostCooldown = false;
        }

        if (stopCooldown && stopCooldownTimer.getElapsedTime().asSeconds() >= stopCooldownTime) {
            stopCooldown = false;
        }
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

        float sinA = sin(rotateAngle);
        float cosA = cos(rotateAngle);
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

        // Ограничение расстояния
        float maxStep = segmentSpacing * 2.f;
        if (distance(newHead, head) > maxStep) {
            newHead = head + normalize(newHead - head) * maxStep;
        }

        if (distance(newHead, GameConfig::arenaCenter) > GameConfig::arenaRadius) {
            *this = Worm(GameConfig::arenaCenter, GameConfig::initialLength);
            return;
        }

        segments.push_front(newHead);
        // Сглаживание сегментов
        for (size_t i = 1; i < segments.size(); i++) {
            sf::Vector2f dir = segments[i-1] - segments[i];
            float dist = distance(segments[i-1], segments[i]);
            if (dist > segmentSpacing) {
                segments[i] = segments[i-1] - dir * (segmentSpacing / dist);
            }
        }
        if (growthLeft > 0) --growthLeft;
        else if (segments.size() > 10) segments.pop_back();
        if (segments.size() > max_length) segments.pop_back();

        if (isBoosting && canBoost() && boostDrainTimer.getElapsedTime().asSeconds() >= boostDrainInterval) {
            boostDrainTimer.restart();
            if (growthLeft > 0) --growthLeft;
            else if (segments.size() > 10) segments.pop_back();
        }
    }

    void grow() {
        growthLeft += 10;
        if (scaleRadius < maxScaleRadius)
            scaleRadius += 0.01f;
    }

    virtual void render(sf::RenderWindow& window) {
        float visualRadius = radius * scaleRadius;
        sf::Color c = isGhost ? sf::Color(color.r, color.g, color.b, 100) : color;

        for (auto& pos : segments) {
            sf::CircleShape circle(visualRadius);
            circle.setOrigin(visualRadius, visualRadius);
            circle.setPosition(pos);
            circle.setFillColor(c);
            window.draw(circle);
        }

        // Рисуем глаза (всем червям)
        sf::Vector2f headPos = getHead();
        float eyeRadius = getScaledRadius() * 0.25f;

        // Смещаем глаза относительно направления
        sf::Vector2f perp = {-direction.y, direction.x}; // вектор перпендикуляра
        sf::Vector2f forward = normalize(direction) * getScaledRadius() * 0.6f;
        sf::Vector2f leftEyePos = headPos + perp * 0.5f * getScaledRadius() + forward;
        sf::Vector2f rightEyePos = headPos - perp * 0.5f * getScaledRadius() + forward;

        sf::CircleShape leftEye(eyeRadius);
        leftEye.setFillColor(sf::Color::White);
        leftEye.setOrigin(eyeRadius, eyeRadius);
        leftEye.setPosition(leftEyePos);
        window.draw(leftEye);

        sf::CircleShape rightEye(eyeRadius);
        rightEye.setFillColor(sf::Color::White);
        rightEye.setOrigin(eyeRadius, eyeRadius);
        rightEye.setPosition(rightEyePos);
        window.draw(rightEye);

        // Имя (только ботам)
        if (isBot && !name.empty()) {
            static sf::Font font;
            static bool fontLoaded = false;
            if (!fontLoaded) {
                font.loadFromFile("Arial-BoldItalicMT.ttf"); // или другой font рядом с .exe
                fontLoaded = true;
            }

            sf::Text label(name, font, 8);
            label.setFillColor(sf::Color::White);
            label.setOutlineColor(sf::Color::Black);
            label.setOutlineThickness(2);
            label.setPosition(headPos.x - label.getGlobalBounds().width / 2, headPos.y - getScaledRadius() * 2.5f);
            window.draw(label);
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
    float directionInterval = 8.0f; // Реже меняют направление
    float botTurnRate = 1.5f; // Плавные повороты
    sf::Color color = sf::Color(rand() % 256, rand() % 256, rand() % 256);

    BotWorm(sf::Vector2f startPos, int length) : Worm(startPos, length) {
        isBot = true;

        static const std::vector<std::string> randomNames = {
            "Bot42", "SnakeX", "SlitherAI", "Wiggler", "NeoWorm", "Eater", "Curlz", "Zigzag", "Creeper", "NomNom"
        };
        name = randomNames[rand() % randomNames.size()];

        randomizeDirection();
    }

    void avoidObstacles(const std::vector<BotWorm>& bots, const Worm& player) {
        const float avoidDistance = 50.f;
        const float turnStrength = 1.5f; // Менее резкие манёвры

        sf::Vector2f avoidanceVector{0.f, 0.f};

        // Избегаем игрока (слабо, чтобы были тупее)
        for (const auto& seg : player.segments) {
            float d = distance(getHead(), seg);
            if (d < avoidDistance && d > 0.01f) {
                float weight = (avoidDistance - d) / avoidDistance;
                avoidanceVector += normalize(getHead() - seg) * weight / d * 0.5f; // Уменьшено влияние
            }
        }

        // Избегаем границы арены
        float distToCenter = distance(getHead(), GameConfig::arenaCenter);
        if (distToCenter > GameConfig::arenaRadius * 0.9f) {
            sf::Vector2f toCenter = normalize(GameConfig::arenaCenter - getHead());
            float weight = (distToCenter - GameConfig::arenaRadius * 0.9f) / (GameConfig::arenaRadius * 0.1f);
            avoidanceVector += toCenter * weight * 2.f;
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

    void render(sf::RenderWindow& window) override {
        Worm::render(window);
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
    float size = 8.f;
    sf::Color color = sf::Color(rand() % 256, rand() % 256, rand() % 256);
    sf::Clock lifeTimer;
    float maxLifeTime = 5.f;

    Food() { respawn(); }

    void respawn() {
        float angle = (rand() / (float)RAND_MAX) * 2 * 3.14159f;
        float radius = std::sqrt(rand() / (float)RAND_MAX) * GameConfig::arenaRadius;
        position = GameConfig::arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
        color = sf::Color(rand() % 256, rand() % 256, rand() % 256);
        lifeTimer.restart();
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

    bool isExpired() {
        return lifeTimer.getElapsedTime().asSeconds() >= maxLifeTime;
    }
};

void spawnFoodFromWorm(const Worm& worm, std::vector<Food>& foods) {
    for (size_t i = 0; i < worm.segments.size(); i += 10) {
        Food f;
        f.position = worm.segments[i];
        f.size = 6.f + static_cast<float>(rand() % 5);
        foods.push_back(f);
    }
}

int main(int argc, char* argv[]) {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    int port = (argc > 1) ? std::stoi(argv[1]) : 12345;
    std::string window_title = "Wormax Mini with Bots - " + std::to_string(port);
    sf::RenderWindow window(sf::VideoMode(800, 600), window_title);
    window.setFramerateLimit(60);

    sf::Font font;
    if (!font.loadFromFile("Arial-BoldItalicMT.ttf")) {
        std::cerr << "Не удалось загрузить sansation.ttf — текст не будет отображён\n";
    }

    sf::Text scoreText;
    scoreText.setFont(font);  // ВАЖНО: без этого текст не покажется
    scoreText.setCharacterSize(16);
    scoreText.setFillColor(sf::Color::White);
    scoreText.setPosition(10.f, 10.f);

    sf::CircleShape boost_circle(20.f); // ~16x16 пикселей, ~3 пикселя в 32x32
    sf::CircleShape stop_circle(20.f);
    sf::CircleShape ghost_circle(20.f);
    boost_circle.setPosition(340, 550); // ~15,22 в 32x32
    stop_circle.setPosition(400, 550);  // ~16,22 в 32x32
    ghost_circle.setPosition(460, 550); // ~17,22 в 32x32
    sf::Color active_color(0, 255, 0); // ~150 в grayscale
    sf::Color inactive_color(0, 100, 0); // ~59 в grayscale

    float spawnAngle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
    float spawnRadius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * GameConfig::arenaRadius;
    sf::Vector2f spawnPos = GameConfig::arenaCenter + sf::Vector2f(std::cos(spawnAngle), std::sin(spawnAngle)) * spawnRadius;
    Worm player(spawnPos, GameConfig::initialLength);

    sf::View view({0, 0, 400, 300});
    view.setCenter(GameConfig::arenaCenter);
    view.zoom(0.6f);
    window.setView(view);

    std::vector<Food> foods(GameConfig::foodCount);
    std::vector<BotWorm> bots;

    for (int i = 0; i < GameConfig::botCount; ++i) {
        float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
        float radius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * GameConfig::arenaRadius;
        sf::Vector2f pos = GameConfig::arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
        bots.emplace_back(pos, randomInRange(20,100));
    }

    sf::Clock clock;
    bool debugToggle = false;
    std::ofstream rewardLog("rewards.txt");

    std::thread rewardThread(rewardServer);
    rewardThread.detach();

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();
            if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F1) {
                debugToggle = !debugToggle;
            }
        }

        float deltaTime = std::min(clock.restart().asSeconds(), 1.f / 30.f);

        player.setBoosting(sf::Keyboard::isKeyPressed(sf::Keyboard::Q));
        player.setStopped(sf::Keyboard::isKeyPressed(sf::Keyboard::W));
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::E) && !player.isGhost && !player.ghostCooldown && player.canGhost()) {
            player.activateGhost();
        }


        // Обновление кружочков с учётом кулдаунов
        boost_circle.setFillColor(player.canBoost() ? active_color : inactive_color);
        stop_circle.setFillColor(
            (player.canStop() && !player.stopCooldown) ? active_color : inactive_color
        );
        ghost_circle.setFillColor(
            (player.canGhost() && !player.ghostCooldown && !player.isGhost) ? active_color : inactive_color
        );
        player.updateDirection(window.mapPixelToCoords(sf::Mouse::getPosition(window), view), deltaTime);
        player.moveForward(deltaTime);

        std::vector<Worm*> allWorms = {&player};
        std::unordered_set<Worm*> deadWorms;
        for (auto& bot : bots) allWorms.push_back(&bot);

        float reward = 0.01f;
        for (size_t i = 0; i < allWorms.size(); ++i) {
            for (size_t j = 0; j < allWorms.size(); ++j) {
                if (i == j) continue;
                Worm* a = allWorms[i];
                Worm* b = allWorms[j];
                if (a->checkCollisionWith(*b)) {
                    deadWorms.insert(a);
                } else if (a->checkHeadOnCollision(*b)) {
                    int lenA = a->segments.size();
                    int lenB = b->segments.size();
                    if (lenA < lenB) deadWorms.insert(a);
                    else if (lenB < lenA) deadWorms.insert(b);
                    else { deadWorms.insert(a); deadWorms.insert(b); }
                }
            }
        }

        std::vector<BotWorm> newBots;
        for (auto* worm : deadWorms) {
            spawnFoodFromWorm(*worm, foods);
            if (worm == &player) {
                float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
                float radius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * GameConfig::arenaRadius;
                sf::Vector2f pos = GameConfig::arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
                player = Worm(pos, GameConfig::initialLength);
                reward -= 100.f;
                rewardLog << "Death: -100\n";
            } else {
                float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
                float radius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * GameConfig::arenaRadius;
                sf::Vector2f pos = GameConfig::arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
                newBots.emplace_back(pos, 20);
                reward += 50.f;
                rewardLog << "Bot killed: +50\n";
                if (debugToggle) {
                    std::cout << "Bot respawned at (" << pos.x << ", " << pos.y << ")\n";
                }
            }
        }

        for (auto& bot : bots) {
            if (deadWorms.find(&bot) == deadWorms.end()) {
                newBots.push_back(std::move(bot));
            }
        }
        bots = std::move(newBots);

        for (auto& bot : bots) bot.update(deltaTime, window.getSize(), bots, player);

        for (auto& food : foods) {
            if (food.isEatenBy(player)) {
                player.grow();
                food.respawn();
                reward += 10.f;
                rewardLog << "Food: +10\n";
            }
        }

        if (player.getScaledRadius() > 10.f) {
            reward -= 0.05f;
            rewardLog << "Radius penalty: -0.05\n";
        }

        float minFoodDist = std::numeric_limits<float>::max();
        for (const auto& food : foods) {
            float d = distance(player.getHead(), food.position);
            if (d < minFoodDist) minFoodDist = d;
        }
        reward += 0.1f * (1.f - minFoodDist / GameConfig::arenaRadius);
        rewardLog << "Food proximity: +" << 0.1f * (1.f - minFoodDist / GameConfig::arenaRadius) << "\n";

        float minBotDist = std::numeric_limits<float>::max();
        for (const auto& bot : bots) {
            float d = distance(player.getHead(), bot.getHead());
            if (d < minBotDist) minBotDist = d;
        }
        if (minBotDist < 50.f) {
            reward -= 0.1f;
            rewardLog << "Bot proximity: -0.1\n";
        }

        rewardLog << "Survival: +0.01\n";
        rewardLog << "Total reward: " << reward << "\n" << std::flush;

        if (debugToggle) {
            std::cout << "Segments: " << player.segments.size() << ", Radius: " << player.getScaledRadius() << ", Reward: " << reward << "\n";
        }

        view.setCenter(player.getHead());
        window.setView(view);

        window.clear(sf::Color::Black);

        sf::CircleShape arenaBorder(GameConfig::arenaRadius);
        arenaBorder.setOrigin(GameConfig::arenaRadius, GameConfig::arenaRadius);
        arenaBorder.setPosition(GameConfig::arenaCenter);
        arenaBorder.setFillColor(sf::Color(30, 30, 30));
        arenaBorder.setOutlineColor(sf::Color(100, 100, 100));
        arenaBorder.setOutlineThickness(10);
        window.draw(arenaBorder);

        for (auto& food : foods) food.render(window);
        for (auto& bot : bots) bot.render(window);
        player.render(window);

        window.setView(window.getDefaultView());
        scoreText.setString("Length: " + std::to_string(player.segments.size()));
        window.draw(scoreText);
        window.draw(boost_circle);
        window.draw(stop_circle);
        window.draw(ghost_circle);
        window.setView(view);

        window.display();
    }

    rewardLog.close();
    return 0;
}