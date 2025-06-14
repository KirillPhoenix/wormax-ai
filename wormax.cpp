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
#include <cstring>

// Global variable for tracking the last step's reward
float g_last_step_reward = 0.0f;
sf::TcpListener rewardListener;
sf::TcpSocket rewardSocket;
std::thread rewardThread;
sf::Vector2f smoothedTargetDir = {0.f, 0.f}; // Сглаженный target

// Добавить глобальные переменные для второго сокета
sf::TcpListener controlListener;
sf::TcpSocket controlSocket;
std::thread controlThread;
sf::Vector2f targetPos; // Целевая позиция для направления
bool boostKey = false, stopKey = false, ghostKey = false; // Состояния клавиш
bool useSocketControl = false; // Флаг для переключения на сокет

// Сервер для приёма команд
void controlServer(int port) {
    while (true) {
        std::cout << "Restarting control listener on port " << port + 1 << "..." << std::endl;
        if (controlListener.listen(port + 1) != sf::Socket::Done) {
            std::cerr << "Failed to listen on port " << port + 1 << ", error: " << controlListener.Error << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        std::cout << "Listening on port " << port + 1 << " succeeded" << std::endl;
        std::cout << "Waiting for client connection on port " << port + 1 << "..." << std::endl;
        if (controlListener.accept(controlSocket) != sf::Socket::Done) {
            std::cerr << "Failed to accept control connection on port " << port + 1 << ", error: " << controlSocket.Error << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        useSocketControl = true;
        std::cout << "Control server started on port " << port + 1 << std::endl;
        while (true) {
            char buffer[11];
            std::size_t received = 0;
            if (controlSocket.receive(buffer, sizeof(buffer), received) == sf::Socket::Done && received == 11) {
                float dx, dy;
                bool boost, stop, ghost;

                // Ручное переворачивание байтов для big-endian
                unsigned char dx_bytes[4], dy_bytes[4];
                for (int i = 0; i < 4; i++) {
                    dx_bytes[i] = buffer[3 - i];  // Переворачиваем байты dx
                    dy_bytes[i] = buffer[7 - i];  // Переворачиваем байты dy
                }
                std::memcpy(&dx, dx_bytes, 4);
                std::memcpy(&dy, dy_bytes, 4);

                boost = buffer[8] != 0;
                stop = buffer[9] != 0;
                ghost = buffer[10] != 0;

                targetPos = sf::Vector2f(dx * 500.f, dy * 500.f);  // Увеличим масштаб до 500
             
                boostKey = boost;
                stopKey = stop;
                ghostKey = ghost;
                std::cout << "✅ Received: dx=" << dx << ", dy=" << dy
                        << ", boost=" << boost << ", stop=" << stop << ", ghost=" << ghost
                        << ", targetPos=(" << targetPos.x << ", " << targetPos.y << ")" << std::endl;   
            } else if (received == 0) {
                std::cerr << "Connection closed by client on port " << port + 1 << std::endl;
            } else {
                std::cerr << "Partial data received: " << received << " bytes on port " << port + 1 << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

// Runs a server to send rewards to a client over TCP at 30 FPS
void rewardServer(int port) {
    rewardListener.listen(port); // ← теперь порт передаётся
    if (rewardListener.accept(rewardSocket) != sf::Socket::Done) {
        std::cerr << "Failed to accept reward connection on port " << port << std::endl;
        return;
    }

    rewardListener.accept(rewardSocket); // Accept client connection
    while (true) {
        sf::Packet packet;
        packet << g_last_step_reward; // Send the latest reward
        rewardSocket.send(packet);
        std::this_thread::sleep_for(std::chrono::milliseconds(30)); // 30 FPS
    }
}

// Consolidated game settings for easy configuration
struct Settings {
    // Arena settings
    float arenaRadius = 1000.f; // Radius of the game arena
    sf::Vector2f arenaCenter = {arenaRadius, arenaRadius}; // Center of the arena
    sf::Color arenaFillColor = sf::Color(30, 30, 30); // Arena background color
    sf::Color arenaOutlineColor = sf::Color(220, 10, 10); // Arena border color
    float arenaOutlineThickness = 10.f; // Thickness of arena border

    // Worm settings
    int initialWormLength = 30; // Initial length of worms
    float wormRadius = 6.f; // Base radius of worm segments
    float maxWormScaleRadius = 2.5f; // Maximum scaling factor for worm radius
    float wormSegmentSpacing = 10.f; // Distance between worm segments
    float wormNormalSpeed = 150.f; // Normal movement speed
    float wormFastSpeed = 300.f; // Boosted movement speed
    float wormMaxTurnRate = 15.0f; // Maximum turning rate (degrees per second)
    size_t wormMaxLength = 300; // Maximum number of segments
    float ghostDuration = 2.f; // Duration of ghost mode
    float ghostCooldownTime = 10.f; // Cooldown time for ghost mode
    float boostDrainInterval = 1.f; // Interval for boost resource drain
    float stopCooldownTime = 3.f; // Cooldown time for stop ability
    int minBoostLength = 10; // Minimum length to use boost
    int minStopLength = 100; // Minimum length to use stop
    int minGhostLength = 200; // Minimum length to use ghost
    float eyeRadiusScale = 0.25f; // Eye size relative to worm radius
    float eyeOffsetScale = 0.5f; // Eye position offset from head
    float eyeForwardOffset = 0.6f; // Forward offset for eyes

    // Bot settings
    int botCount = 10; // Number of bots
    float botTurnRate = 1.5f; // Bot turning rate (slower than player)
    float botDirectionInterval = 8.0f; // Time before bot changes direction
    float botAvoidDistance = 50.f; // Distance to avoid obstacles
    float botTurnStrength = 1.5f; // Strength of avoidance maneuvers
    int botMinLength = 20; // Minimum initial bot length
    int botMaxLength = 100; // Maximum initial bot length

    // Food settings
    int foodCount = 60; // Number of food items
    float foodSize = 8.f; // Size of food items
    float foodMaxLifeTime = 5.f; // Maximum lifetime of food
    float foodDropSizeMin = 6.f; // Minimum size of food dropped from worms
    int foodDropSegmentSkip = 10; // Drop food every N segments of dead worm

    // View settings
    float viewZoom = 0.6f; // Initial zoom level
    sf::Vector2f viewSize = {400.f, 300.f}; // Initial view size

    // UI settings
    int scoreFontSize = 16; // Font size for score display
    sf::Vector2f scorePosition = {10.f, 10.f}; // Position of score text
    float abilityCircleSize = 20.f; // Size of ability indicator circles
    sf::Vector2f boostCirclePos = {340.f, 550.f}; // Position of boost indicator
    sf::Vector2f stopCirclePos = {400.f, 550.f}; // Position of stop indicator
    sf::Vector2f ghostCirclePos = {460.f, 550.f}; // Position of ghost indicator
    sf::Color activeColor = sf::Color(0, 255, 0); // Color for active abilities
    sf::Color inactiveColor = sf::Color(0, 100, 0); // Color for inactive abilities
    int botLabelFontSize = 8; // Font size for bot names
    float botLabelOffset = 2.5f; // Offset for bot name above head

    // Game mechanics
    float maxStepDistance = wormSegmentSpacing * 2.f; // Maximum movement step
    float foodProximityRewardFactor = 0.1f; // Reward factor for food proximity
    float radiusPenalty = 0.05f; // Penalty for large radius
    float botProximityPenaltyDistance = 50.f; // Distance for bot proximity penalty
    float botProximityPenalty = 0.1f; // Penalty for being near bots
    float survivalReward = 0.01f; // Reward for surviving each frame
    float foodReward = 10.f; // Reward for eating food
    float botKillReward = 50.f; // Reward for killing a bot
    float deathPenalty = 100.f; // Penalty for player death
};

// Utility function to calculate distance between two points
float distance(sf::Vector2f a, sf::Vector2f b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

// Utility function to calculate vector length
float length(sf::Vector2f v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

// Utility function to normalize a vector
sf::Vector2f normalize(sf::Vector2f v) {
    float len = length(v);
    return len == 0 ? sf::Vector2f(0, 0) : v / len;
}

// Utility function to compute dot product
float dot(sf::Vector2f a, sf::Vector2f b) {
    return a.x * b.x + a.y * b.y;
}

// Utility function to compute cross product
float cross(sf::Vector2f a, sf::Vector2f b) {
    return a.x * b.y - a.y * b.x;
}

// Utility function to calculate angle between two vectors
float angleBetween(sf::Vector2f a, sf::Vector2f b) {
    a = normalize(a);
    b = normalize(b);
    float dotVal = std::clamp(dot(a, b), -1.f, 1.f);
    return std::acos(dotVal);
}

// Utility function to generate random integer in range [a, b]
int randomInRange(int a, int b) {
    return a + rand() % (b - a + 1);
}

// Represents a worm (player or bot) in the game
class Worm {
public:
    std::deque<sf::Vector2f> segments; // Worm body segments
    float segmentSpacing; // Distance between segments
    float normalSpeed; // Normal movement speed
    float fastSpeed; // Boosted movement speed
    float currentSpeed; // Current movement speed
    float radius; // Base radius of segments
    float scaleRadius; // Current radius scaling factor
    float maxScaleRadius; // Maximum radius scaling factor
    int growthLeft; // Segments left to grow
    size_t max_length; // Maximum number of segments
    sf::Color color; // Worm color
    sf::Vector2f direction; // Movement direction
    float maxTurnRate; // Maximum turning rate
    bool isGhost; // Ghost mode status
    bool isBoosting; // Boosting status
    bool isStopped; // Stopped status
    sf::Clock ghostTimer; // Timer for ghost mode duration
    sf::Clock ghostCooldownTimer; // Timer for ghost mode cooldown
    sf::Clock boostDrainTimer; // Timer for boost resource drain
    float ghostDuration; // Duration of ghost mode
    float ghostCooldownTime; // Cooldown time for ghost mode
    float boostDrainInterval; // Interval for boost resource drain
    bool isGhostCooldown; // Ghost mode cooldown status (renamed for clarity)
    bool stopCooldown; // Stop ability cooldown status
    float stopCooldownTime; // Cooldown time for stop ability
    sf::Clock stopCooldownTimer; // Timer for stop cooldown
    sf::Clock stopDurationTimer; // Timer for stop duration
    bool isBot; // Is this a bot?
    std::string name; // Worm name (for bots)
    const Settings& settings; // Reference to game settings

    // Constructor initializes worm with starting position and length
    Worm(sf::Vector2f startPos, int length, const Settings& settings)
        : settings(settings), segmentSpacing(settings.wormSegmentSpacing), normalSpeed(settings.wormNormalSpeed),
          fastSpeed(settings.wormFastSpeed), currentSpeed(settings.wormNormalSpeed), radius(settings.wormRadius),
          scaleRadius(1.f), maxScaleRadius(settings.maxWormScaleRadius), growthLeft(length),
          max_length(settings.wormMaxLength), color(sf::Color(rand() % 256, rand() % 256, rand() % 256)),
          direction({1.f, 0.f}), maxTurnRate(settings.wormMaxTurnRate), isGhost(false), isBoosting(false),
          isStopped(false), ghostDuration(settings.ghostDuration), ghostCooldownTime(settings.ghostCooldownTime),
          boostDrainInterval(settings.boostDrainInterval), isGhostCooldown(false), stopCooldown(false),
          stopCooldownTime(settings.stopCooldownTime), isBot(false), name("") {
        segments.push_back(startPos);
    }

    // Resets worm to initial state at a new position
    void reset(sf::Vector2f startPos, int length) {
        segments.clear();
        segments.push_back(startPos);
        growthLeft = length;
        scaleRadius = 1.f;
        direction = {1.f, 0.f};
        currentSpeed = normalSpeed;
        isGhost = false;
        isBoosting = false;
        isStopped = false;
        isGhostCooldown = false;
        stopCooldown = false;
        ghostTimer.restart();
        ghostCooldownTimer.restart();
        boostDrainTimer.restart();
        stopCooldownTimer.restart();
        stopDurationTimer.restart();
        color = sf::Color(rand() % 256, rand() % 256, rand() % 256);
    }

    // Sets boosting state and updates speed
    void setBoosting(bool boosting) {
        isBoosting = boosting;
        currentSpeed = (boosting && canBoost()) ? fastSpeed : normalSpeed;
    }

    // Sets stopped state with cooldown and duration checks
    void setStopped(bool keyPressed) {
        updateBonuses();
        if (keyPressed && !isStopped && !stopCooldown && canStop()) {
            isStopped = true;
            stopDurationTimer.restart();
        }
        if (isStopped) {
            bool expired = stopDurationTimer.getElapsedTime().asSeconds() >= settings.stopCooldownTime;
            if (!keyPressed || expired) {
                isStopped = false;
                stopCooldown = true;
                stopCooldownTimer.restart();
            }
        }
    }

    // Checks if worm can use ghost mode
    bool canGhost() const { return segments.size() >= settings.minGhostLength; }

    // Checks if worm can use stop ability
    bool canStop() const { return segments.size() >= settings.minStopLength; }

    // Checks if worm can use boost
    bool canBoost() const { return segments.size() > settings.minBoostLength; }

    // Activates ghost mode if available
    void activateGhost() {
        if (isGhost || isGhostCooldown || !canGhost()) return;
        isGhost = true;
        ghostTimer.restart();
    }

    // Updates status of bonuses (ghost, stop, boost)
    void updateBonuses() {
        if (isGhost && ghostTimer.getElapsedTime().asSeconds() >= ghostDuration) {
            isGhost = false;
            isGhostCooldown = true;
            ghostCooldownTimer.restart();
        }
        if (isGhostCooldown && ghostCooldownTimer.getElapsedTime().asSeconds() >= ghostCooldownTime) {
            isGhostCooldown = false;
        }
        if (stopCooldown && stopCooldownTimer.getElapsedTime().asSeconds() >= stopCooldownTime) {
            stopCooldown = false;
        }
    }

    // Updates worm direction towards a target
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

    // Moves worm forward, handling collisions and growth
    void moveForward(float deltaTime) {
        updateBonuses();
        if (isStopped) return;
        sf::Vector2f head = segments.front();
        sf::Vector2f newHead = head + direction * currentSpeed * deltaTime;
        // Limit movement step to prevent tunneling
        if (distance(newHead, head) > settings.maxStepDistance) {
            newHead = head + normalize(newHead - head) * settings.maxStepDistance;
        }
        // Check for arena boundary collision
        if (distance(newHead, settings.arenaCenter) > settings.arenaRadius) {
            reset(settings.arenaCenter, settings.initialWormLength);
            return;
        }
        segments.push_front(newHead);
        // Smooth segment positions
        for (size_t i = 1; i < segments.size(); i++) {
            sf::Vector2f dir = segments[i-1] - segments[i];
            float dist = distance(segments[i-1], segments[i]);
            if (dist > segmentSpacing) {
                segments[i] = segments[i-1] - dir * (segmentSpacing / dist);
            }
        }
        // Handle growth and length limits
        if (growthLeft > 0) --growthLeft;
        else if (segments.size() > settings.minBoostLength) segments.pop_back();
        if (segments.size() > max_length) segments.pop_back();
        // Handle boost resource drain
        if (isBoosting && canBoost() && boostDrainTimer.getElapsedTime().asSeconds() >= boostDrainInterval) {
            boostDrainTimer.restart();
            if (growthLeft > 0) --growthLeft;
            else if (segments.size() > settings.minBoostLength) segments.pop_back();
        }
    }

    // Increases worm size
    void grow() {
        growthLeft += 10;
        if (scaleRadius < maxScaleRadius) scaleRadius += 0.01f;
    }

    // Renders the worm and its features (eyes, name)
    virtual void render(sf::RenderWindow& window) {
        float visualRadius = radius * scaleRadius;
        sf::Color c = isGhost ? sf::Color(color.r, color.g, color.b, 100) : color;
        // Draw segments
        for (auto& pos : segments) {
            sf::CircleShape circle(visualRadius);
            circle.setOrigin(visualRadius, visualRadius);
            circle.setPosition(pos);
            circle.setFillColor(c);
            window.draw(circle);
        }
        // Draw eyes
        sf::Vector2f headPos = getHead();
        float eyeRadius = getScaledRadius() * settings.eyeRadiusScale;
        sf::Vector2f perp = {-direction.y, direction.x}; // Perpendicular vector
        sf::Vector2f forward = normalize(direction) * getScaledRadius() * settings.eyeForwardOffset;
        sf::Vector2f leftEyePos = headPos + perp * settings.eyeOffsetScale * getScaledRadius() + forward;
        sf::Vector2f rightEyePos = headPos - perp * settings.eyeOffsetScale * getScaledRadius() + forward;
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
        // Draw name for bots
        if (isBot && !name.empty()) {
            static sf::Font font;
            static bool fontLoaded = false;
            if (!fontLoaded) {
                font.loadFromFile("Arial-BoldItalicMT.ttf");
                fontLoaded = true;
            }
            sf::Text label(name, font, settings.botLabelFontSize);
            label.setFillColor(sf::Color::White);
            label.setOutlineColor(sf::Color::Black);
            label.setOutlineThickness(2);
            label.setPosition(headPos.x - label.getGlobalBounds().width / 2, headPos.y - getScaledRadius() * settings.botLabelOffset);
            window.draw(label);
        }
    }

    // Returns the head position
    sf::Vector2f getHead() const { return segments.front(); }

    // Returns the scaled radius
    float getScaledRadius() const { return radius * scaleRadius; }

    // Checks collision with another worm
    bool checkCollisionWith(const Worm& other) const {
        if (isGhost || other.isGhost) return false;
        for (size_t i = 1; i < other.segments.size(); ++i) {
            if (distance(getHead(), other.segments[i]) < getScaledRadius() + other.getScaledRadius())
                return true;
        }
        return false;
    }

    // Checks head-on collision with another worm
    bool checkHeadOnCollision(const Worm& other) const {
        if (isGhost || other.isGhost) return false;
        return distance(getHead(), other.getHead()) < getScaledRadius() + other.getScaledRadius();
    }
};

// Bot worm with AI behavior
class BotWorm : public Worm {
public:
    float directionTimer; // Time since last direction change
    float directionInterval; // Time before changing direction
    float botTurnRate; // Bot-specific turn rate

    // Constructor initializes bot with random name and direction
    BotWorm(sf::Vector2f startPos, int length, const Settings& settings)
        : Worm(startPos, length, settings), directionTimer(0.f), directionInterval(settings.botDirectionInterval),
          botTurnRate(settings.botTurnRate) {
        isBot = true;
        static const std::vector<std::string> randomNames = {
            "Bot42", "SnakeX", "SlitherAI", "Wiggler", "NeoWorm", "Eater", "Curlz", "Zigzag", "Creeper", "NomNom"
        };
        name = randomNames[rand() % randomNames.size()];
        randomizeDirection();
    }

    // Avoids obstacles (player, other bots, arena boundary)
    void avoidObstacles(const std::vector<BotWorm>& bots, const Worm& player) {
        sf::Vector2f avoidanceVector{0.f, 0.f};
        // Avoid player
        for (const auto& seg : player.segments) {
            float d = distance(getHead(), seg);
            if (d < settings.botAvoidDistance && d > 0.01f) {
                float weight = (settings.botAvoidDistance - d) / settings.botAvoidDistance;
                avoidanceVector += normalize(getHead() - seg) * weight / d * 0.5f;
            }
        }
        // Avoid arena boundary
        float distToCenter = distance(getHead(), settings.arenaCenter);
        if (distToCenter > settings.arenaRadius * 0.9f) {
            sf::Vector2f toCenter = normalize(settings.arenaCenter - getHead());
            float weight = (distToCenter - settings.arenaRadius * 0.9f) / (settings.arenaRadius * 0.1f);
            avoidanceVector += toCenter * weight * 2.f;
        }

        // Avoid other bots
        for (const auto& other : bots) {
            if (&other == this) continue; // не избегаем самих себя
            for (const auto& seg : other.segments) {
                float d = distance(getHead(), seg);
                if (d < settings.botAvoidDistance && d > 0.01f) {
                    float weight = (settings.botAvoidDistance - d) / settings.botAvoidDistance;
                    avoidanceVector += normalize(getHead() - seg) * weight / d * 0.5f;
                }
            }
        }

        if (length(avoidanceVector) > 0.01f) {
            sf::Vector2f newDir = normalize(direction + avoidanceVector * settings.botTurnStrength);
            direction = newDir;
        }
    }

    // Updates bot movement and direction
    void update(float deltaTime, const sf::Vector2u& winSize, const std::vector<BotWorm>& bots, const Worm& player) {
        directionTimer += deltaTime;
        if (directionTimer >= directionInterval) {
            directionTimer = 0.f;
            sf::Vector2f projected = getHead() + direction * 50.f;
            bool nearObstacle = distance(projected, player.getHead()) < 40.f;
            if (!nearObstacle) randomizeDirection();
        }
        avoidObstacles(bots, player);
        float oldRate = maxTurnRate;
        maxTurnRate = botTurnRate;
        updateDirection(getHead() + direction * 100.f, deltaTime);
        maxTurnRate = oldRate;
        moveForward(deltaTime);
    }

    // Renders the bot
    void render(sf::RenderWindow& window) override {
        Worm::render(window);
    }

private:
    // Randomizes bot direction
    void randomizeDirection() {
        float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
        direction = {std::cos(angle), std::sin(angle)};
    }
};

// Represents food items in the game
class Food {
public:
    sf::Vector2f position; // Food position
    float size; // Food size
    sf::Color color; // Food color
    sf::Clock lifeTimer; // Timer for food lifetime
    float maxLifeTime; // Maximum lifetime
    const Settings* settings;
    bool temporary = false;


    // Constructor initializes food
    Food(const Settings& s) : settings(&s) {
        respawn();
    }

    // Respawns food at a random position
    void respawn() {
        float angle = (rand() / (float)RAND_MAX) * 2 * 3.14159f;
        float radius = std::sqrt(rand() / (float)RAND_MAX) * settings->arenaRadius;
        position = settings->arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
        color = sf::Color(rand() % 256, rand() % 256, rand() % 256);
        size = settings->foodDropSizeMin + static_cast<float>(rand() % 5); 
        lifeTimer.restart();
    }

    // Renders the food
    void render(sf::RenderWindow& window) {
        sf::CircleShape circle(size / 2);
        circle.setFillColor(color);
        circle.setOrigin(size / 2, size / 2);
        circle.setPosition(position);
        window.draw(circle);
    }

    // Checks if food is eaten by a worm
    bool isEatenBy(const Worm& worm) {
        return distance(worm.getHead(), position) < worm.getScaledRadius() + size / 2;
    }

    // Checks if food has expired
    bool isExpired() const {
        return temporary && lifeTimer.getElapsedTime().asSeconds() >= maxLifeTime;
    }

};

// Spawns food from a dead worm's segments
void spawnFoodFromWorm(const Worm& worm, std::vector<Food>& foods, const Settings& settings) {
    for (size_t i = 0; i < worm.segments.size(); i += settings.foodDropSegmentSkip) {
        Food f(settings);
        f.position = worm.segments[i];
        f.size = settings.foodDropSizeMin + static_cast<float>(rand() % 5);
        f.temporary = true;
        f.color = sf::Color(
            randomInRange(100, 255),
            randomInRange(100, 255),
            randomInRange(100, 255)
        );
        f.maxLifeTime = 10.f; // ⏳ или settings.foodDropLifetime, если добавишь это в Settings
        foods.push_back(std::move(f));
    }
}

// Main game loop
int main(int argc, char* argv[]) {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    Settings settings; // Initialize game settings
    int port = (argc > 1) ? std::stoi(argv[1]) : 12345; // Default port
    std::string window_title = "Wormax Mini with Bots - " + std::to_string(port);
    sf::RenderWindow window(sf::VideoMode(800, 600), window_title);
    window.setFramerateLimit(60); // Cap at 60 FPS
    // Load font for UI
    sf::Font font;
    if (!font.loadFromFile("Arial-BoldItalicMT.ttf")) {
        std::cerr << "Failed to load Arial-BoldItalicMT.ttf — text will not be displayed\n";
    }
    // Setup score display
    sf::Text scoreText;
    scoreText.setFont(font);
    scoreText.setCharacterSize(settings.scoreFontSize);
    scoreText.setFillColor(sf::Color::White);
    scoreText.setPosition(settings.scorePosition);
    // Setup ability indicators
    sf::CircleShape boost_circle(settings.abilityCircleSize);
    sf::CircleShape stop_circle(settings.abilityCircleSize);
    sf::CircleShape ghost_circle(settings.abilityCircleSize);
    boost_circle.setPosition(settings.boostCirclePos);
    stop_circle.setPosition(settings.stopCirclePos);
    ghost_circle.setPosition(settings.ghostCirclePos);
    // Initialize player
    float spawnAngle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
    float spawnRadius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * settings.arenaRadius;
    sf::Vector2f spawnPos = settings.arenaCenter + sf::Vector2f(std::cos(spawnAngle), std::sin(spawnAngle)) * spawnRadius;
    Worm player(spawnPos, settings.initialWormLength, settings);
    // Setup camera view
    sf::View view(settings.arenaCenter, settings.viewSize);
    view.zoom(settings.viewZoom);
    window.setView(view);
    // Initialize food
    std::vector<Food> foods;
    foods.reserve(settings.foodCount); // не обязательно, но быстрее

    for (int i = 0; i < settings.foodCount; ++i) {
        foods.emplace_back(settings);
    }    // Initialize bots
    std::vector<BotWorm> bots;
    for (int i = 0; i < settings.botCount; ++i) {
        float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
        float radius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * settings.arenaRadius;
        sf::Vector2f pos = settings.arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
        bots.emplace_back(pos, randomInRange(settings.botMinLength, settings.botMaxLength), settings);
    }
    
    sf::Clock clock;
    bool debugToggle = false; // Debug mode toggle
    
    //std::ofstream //rewardLog("rewards.txt"); // Log file for rewards
    std::thread rewardThread(rewardServer, port);
    rewardThread.detach(); // Run reward server in background
    
    // Запуск сервера управления
    controlThread = std::thread(controlServer, port);
    controlThread.detach();

    // Main game loop
    while (window.isOpen()) {
        // Handle events
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed || 
                (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape)) {
                window.close(); // Закрытие по Esc или крестику
            }
            
            if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F1) {
                debugToggle = !debugToggle;
            }
        }
        // Cap delta time to prevent large jumps
        float deltaTime = std::min(clock.restart().asSeconds(), 1.f / 30.f);

        // Управление: сокет или мышь/клавиатура
        if (useSocketControl) {
            sf::Vector2f rawTargetDir = targetPos - player.getHead();
            std::cout << "Raw target dir: (" << rawTargetDir.x << ", " << rawTargetDir.y << "), length=" << length(rawTargetDir) << std::endl;
            if (length(rawTargetDir) < 0.1f) {
                rawTargetDir = player.direction;
                std::cout << "Using current direction due to small length" << std::endl;
            }
            smoothedTargetDir = rawTargetDir;
            sf::Vector2f target = player.getHead() + normalize(smoothedTargetDir) * 500.f;  // Увеличим расстояние до цели
            std::cout << "Final target: (" << target.x << ", " << target.y << ")" << std::endl;
            player.updateDirection(target, deltaTime);
            player.setBoosting(boostKey);
            player.setStopped(stopKey);
            if (ghostKey && !player.isGhost && !player.isGhostCooldown && player.canGhost()) {
                player.activateGhost();
            }
        } else {
            player.setBoosting(sf::Keyboard::isKeyPressed(sf::Keyboard::Q));
            player.setStopped(sf::Keyboard::isKeyPressed(sf::Keyboard::W));
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::E) && !player.isGhost && !player.isGhostCooldown && player.canGhost()) {
                player.activateGhost();
            }
            player.updateDirection(window.mapPixelToCoords(sf::Mouse::getPosition(window), view), deltaTime);
        }

        // Update ability indicators
        boost_circle.setFillColor(player.canBoost() ? settings.activeColor : settings.inactiveColor);
        stop_circle.setFillColor((player.canStop() && !player.stopCooldown) ? settings.activeColor : settings.inactiveColor);
        ghost_circle.setFillColor((player.canGhost() && !player.isGhostCooldown && !player.isGhost) ? settings.activeColor : settings.inactiveColor);
        // Update player movement
        //player.updateDirection(window.mapPixelToCoords(sf::Mouse::getPosition(window), view), deltaTime); првторный вызов, выше уже используется
        player.moveForward(deltaTime);
        // Collect all worms for collision checks
        std::vector<Worm*> allWorms = {&player};
        for (auto& bot : bots) allWorms.push_back(&bot);
        std::unordered_set<Worm*> deadWorms;
        // Check collisions
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
        // Handle rewards and respawns
        float reward = settings.survivalReward;
        std::vector<BotWorm> newBots;
        for (auto* worm : deadWorms) {
            spawnFoodFromWorm(*worm, foods, settings);
            if (worm == &player) {
                float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
                float radius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * settings.arenaRadius;
                sf::Vector2f pos = settings.arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
                player.reset(pos, settings.initialWormLength);
                reward -= settings.deathPenalty;
                //rewardLog << "Death: -" << settings.deathPenalty << "\n";
            } else {
                float angle = static_cast<float>(rand()) / RAND_MAX * 2 * 3.14159f;
                float radius = std::sqrt(static_cast<float>(rand()) / RAND_MAX) * settings.arenaRadius;
                sf::Vector2f pos = settings.arenaCenter + sf::Vector2f(std::cos(angle), std::sin(angle)) * radius;
                newBots.emplace_back(pos, randomInRange(settings.botMinLength, settings.botMaxLength), settings);
                reward += settings.botKillReward;
                //rewardLog << "Bot killed: +" << settings.botKillReward << "\n";
                if (debugToggle) {
                    std::cout << "Bot respawned at (" << pos.x << ", " << pos.y << ")\n";
                }
            }
        }
        // Update bot list
        for (auto& bot : bots) {
            if (deadWorms.find(&bot) == deadWorms.end()) {
                newBots.push_back(std::move(bot));
            }
        }
        bots = std::move(newBots);
        // Update bots
        for (auto& bot : bots) bot.update(deltaTime, window.getSize(), bots, player);
        // Удалим испорченную еду
        foods.erase(
            std::remove_if(foods.begin(), foods.end(), [](const Food& food) {
                return food.isExpired();
            }),
            foods.end()
        );

        // Проверка еды на поедание
        for (auto& food : foods) {
            if (food.isEatenBy(player)) {
                player.grow();
                food.respawn();  // оставим respawn обычной еды
                reward += settings.foodReward;
                //rewardLog << "Food: +" << settings.foodReward << "\n";
            }
        }
        // Apply radius penalty
        if (player.getScaledRadius() > settings.wormRadius) {
            reward -= settings.radiusPenalty;
            //rewardLog << "Radius penalty: -" << settings.radiusPenalty << "\n";
        }
        // Reward for food proximity
        float minFoodDist = std::numeric_limits<float>::max();
        for (const auto& food : foods) {
            float d = distance(player.getHead(), food.position);
            if (d < minFoodDist) minFoodDist = d;
        }
        reward += settings.foodProximityRewardFactor * (1.f - minFoodDist / settings.arenaRadius);
        //rewardLog << "Food proximity: +" << settings.foodProximityRewardFactor * (1.f - minFoodDist / settings.arenaRadius) << "\n";
        // Penalty for bot proximity
        float minBotDist = std::numeric_limits<float>::max();
        for (const auto& bot : bots) {
            float d = distance(player.getHead(), bot.getHead());
            if (d < minBotDist) minBotDist = d;
        }
        if (minBotDist < settings.botProximityPenaltyDistance) {
            reward -= settings.botProximityPenalty;
            //rewardLog << "Bot proximity: -" << settings.botProximityPenalty << "\n";
        }
        // Log and set reward
        //rewardLog << "Survival: +" << settings.survivalReward << "\n";
        //rewardLog << "Total reward: " << reward << "\n" << std::flush;

        static float prevMinFoodDist = minFoodDist; // Используем уже вычисленный minFoodDist
        float distReward = (prevMinFoodDist - minFoodDist) * 0.1f; // +0.1 за пиксель ближе
        reward += distReward;
        prevMinFoodDist = minFoodDist;
        //rewardLog << "Dist reward: " << distReward << "\n";

        g_last_step_reward = reward;
        // Debug output
        if (debugToggle) {
            std::cout << "Segments: " << player.segments.size() << ", Radius: " << player.getScaledRadius() << ", Reward: " << reward << "\n";
        }
        // Update camera to follow player
        view.setCenter(player.getHead());
        window.setView(view);
        // Clear window
        window.clear(sf::Color::Black);
        // Draw arena
        sf::CircleShape arenaBorder(settings.arenaRadius);
        arenaBorder.setOrigin(settings.arenaRadius, settings.arenaRadius);
        arenaBorder.setPosition(settings.arenaCenter);
        arenaBorder.setFillColor(settings.arenaFillColor);
        arenaBorder.setOutlineColor(settings.arenaOutlineColor);
        arenaBorder.setOutlineThickness(settings.arenaOutlineThickness);
        window.draw(arenaBorder);
        // Draw game objects
        for (auto& food : foods) food.render(window);
        for (auto& bot : bots) bot.render(window);
        player.render(window);
        // Draw UI
        window.setView(window.getDefaultView());
        scoreText.setString("Length: " + std::to_string(player.segments.size()));
        window.draw(scoreText);
        window.draw(boost_circle);
        window.draw(stop_circle);
        window.draw(ghost_circle);
        window.setView(view);
        // Display frame
        window.display();
    }
    //rewardLog.close();
    return 0;
}