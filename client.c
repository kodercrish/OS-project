/**
 * Krish Patel
 * IMT2023134
 * 12th May, 2025
 */

// Academia Portal Client
// CS-513 System Software Mini Project

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <termios.h>
#include <stdbool.h>

#define SERVER_PORT 8080
#define BUFFER_SIZE 1024
#define SERVER_ADDRESS "127.0.0.1"
#define USERNAME_MAX_LENGTH 100
#define PASSWORD_MAX_LENGTH 100
#define COURSE_NAME_MAX_LENGTH 100
#define RESPONSE_MAX_LENGTH 1024

// Global variables
int socketFd = -1;
bool loggedIn = false;
char userRole[20] = "";
int userId = -1;

// Function prototypes
void setupConnection();
void displayLoginScreen();
void displayAdminInterface();
void displayStudentInterface();
void displayFacultyInterface();
void communicateWithServer(const char* request, char* response);
void gracefulExit(int signal_num);
void securePasswordInput(char* password);
void parseResponseToken(const char* str, char* result, int token_index);

// UI helper functions
void clearConsole() {
    printf("\033[2J\033[1;1H"); // ANSI escape sequence to clear screen
}

void showHeader(const char* title) {
    printf("====================================\n");
    printf("      %s\n", title);
    printf("====================================\n");
}

void showErrorMessage(const char* message) {
    printf("\033[31m%s\033[0m\n", message); // Red text
    printf("Press Enter to continue...");
    getchar(); // Wait for Enter key
}

void showSuccessMessage(const char* message) {
    printf("\033[32m%s\033[0m\n", message); // Green text
    printf("Press Enter to continue...");
    getchar(); // Wait for Enter key
}

void pauseExecution() {
    printf("\nPress Enter to continue...");
    getchar(); // Wait for Enter key
}

// Signal handler for cleanup
void gracefulExit(int signal_num) {
    printf("\nExiting client application...\n");
    if (socketFd != -1) {
        close(socketFd);
    }
    exit(signal_num);
}

// Get password without showing it on screen
void securePasswordInput(char* password) {
    struct termios oldSettings, newSettings;
    
    // Get terminal settings
    tcgetattr(STDIN_FILENO, &oldSettings);
    
    // Copy settings
    newSettings = oldSettings;
    
    // Disable echo
    newSettings.c_lflag &= (~ECHO);
    
    // Set new terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &newSettings);
    
    // Read password (safely)
    if (fgets(password, PASSWORD_MAX_LENGTH, stdin) != NULL) {
        // Remove trailing newline if present
        size_t len = strlen(password);
        if (len > 0 && password[len-1] == '\n') {
            password[len-1] = '\0';
        }
    }
    
    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldSettings);
}

// Connect to the server
void setupConnection() {
    // Create socket
    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (socketFd < 0) {
        fprintf(stderr, "Socket creation error\n");
        exit(EXIT_FAILURE);
    }
        // Server address configuration
        struct sockaddr_in serverAddress;
        memset(&serverAddress, 0, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(SERVER_PORT);
        
        // Convert IP address from string to binary form
        if (inet_pton(AF_INET, SERVER_ADDRESS, &serverAddress.sin_addr) <= 0) {
            fprintf(stderr, "Invalid address or address not supported\n");
            exit(EXIT_FAILURE);
        }
        
        // Connect to server
        if (connect(socketFd, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
            fprintf(stderr, "Connection failed: Server might be offline\n");
            exit(EXIT_FAILURE);
        }
        
        printf("Connected to Academia Portal Server\n");
    }
    
    // Send request to server and get response
    void communicateWithServer(const char* request, char* response) {
        // Initialize response buffer
        memset(response, 0, BUFFER_SIZE);
        
        // Send request to server
        if (send(socketFd, request, strlen(request), 0) < 0) {
            fprintf(stderr, "Failed to send request\n");
            strcpy(response, "ERROR");
            return;
        }
        
        // Receive response from server
        ssize_t bytesRead = read(socketFd, response, BUFFER_SIZE - 1);
        
        if (bytesRead < 0) {
            fprintf(stderr, "Failed to read response\n");
            strcpy(response, "ERROR");
            return;
        }
        
        // Ensure null termination
        response[bytesRead] = '\0';
    }
    
    // Extract token from string
    void parseResponseToken(const char* str, char* result, int token_index) {
        char temp[BUFFER_SIZE];
        strcpy(temp, str);
        
        char* token = strtok(temp, " ");
        int i = 0;
        
        while (token != NULL && i < token_index) {
            token = strtok(NULL, " ");
            i++;
        }
        
        if (token != NULL) {
            strcpy(result, token);
        } else {
            result[0] = '\0';
        }
    }
    
    // Login screen
    void displayLoginScreen() {
        while (!loggedIn) {
            clearConsole();
            showHeader("Academia Portal - Login");
            
            char username[USERNAME_MAX_LENGTH];
            char password[PASSWORD_MAX_LENGTH];
            char request[BUFFER_SIZE];
            char response[BUFFER_SIZE];
            
            printf("Username: ");
            scanf("%s", username);
            getchar(); // Clear input buffer
            
            printf("Password: ");
            securePasswordInput(password);
            
            // Send login request
            snprintf(request, BUFFER_SIZE, "LOGIN %s %s", username, password);
            communicateWithServer(request, response);
            
            // Process response
            if (strncmp(response, "LOGIN_SUCCESS", 13) == 0) {
                loggedIn = true;
                
                // Extract user role
                char role[20];
                parseResponseToken(response, role, 1);
                strcpy(userRole, role);
                
                // Extract user ID
                char id[10];
                parseResponseToken(response, id, 2);
                userId = atoi(id);
                
                showSuccessMessage("Login successful!");
            } else {
                // Extract error message
                char errorMsg[BUFFER_SIZE];
                strcpy(errorMsg, "Login failed: ");
                
                // Find position after first space
                char* pos = strchr(response, ' ');
                if (pos != NULL) {
                    strcat(errorMsg, pos + 1);
                } else {
                    strcat(errorMsg, "Unknown error");
                }
                
                showErrorMessage(errorMsg);
            }
        }
        
        // Redirect to appropriate menu based on user role
        if (strcmp(userRole, "ADMIN") == 0) {
            displayAdminInterface();
        } else if (strcmp(userRole, "STUDENT") == 0) {
            displayStudentInterface();
        } else if (strcmp(userRole, "FACULTY") == 0) {
            displayFacultyInterface();
        }
    }
    
    // Admin interface
    void displayAdminInterface() {
        while (loggedIn) {
            clearConsole();
            showHeader("Admin Dashboard");
            
            printf("1. Add Student\n");
            printf("2. Add Faculty\n");
            printf("3. Activate/Deactivate Student\n");
            printf("4. Update Student/Faculty details\n");
            printf("5. View All Users\n");
            printf("6. View All Courses\n");
            printf("7. Logout\n");
            printf("8. Exit\n");
            
            printf("\nEnter your choice: ");
            
            int choice;
            scanf("%d", &choice);
            getchar(); // Clear input buffer
            
            char request[BUFFER_SIZE];
            char response[BUFFER_SIZE];
            
            switch (choice) {
                case 1: { // Add Student
                    clearConsole();
                    showHeader("Add New Student");
                    
                    char username[USERNAME_MAX_LENGTH];
                    char password[PASSWORD_MAX_LENGTH];
                    
                    printf("Enter username: ");
                    scanf("%s", username);
                    getchar(); // Clear input buffer
                    
                    printf("Enter password: ");
                    securePasswordInput(password);
                    
                    snprintf(request, BUFFER_SIZE, "ADMIN %d ADD_STUDENT %s %s", userId, username, password);
                    communicateWithServer(request, response);
                    
                    showSuccessMessage(response);
                    break;
                }
                case 2: { // Add Faculty
                    clearConsole();
                    showHeader("Add New Faculty");
                    
                    char username[USERNAME_MAX_LENGTH];
                    char password[PASSWORD_MAX_LENGTH];
                    
                    printf("Enter username: ");
                    scanf("%s", username);
                    getchar(); // Clear input buffer
                    
                    printf("Enter password: ");
                    securePasswordInput(password);
                    
                    snprintf(request, BUFFER_SIZE, "ADMIN %d ADD_FACULTY %s %s", userId, username, password);
                    communicateWithServer(request, response);
                    
                    showSuccessMessage(response);
                    break;
                }
                case 3: { // Activate/Deactivate Student
                    clearConsole();
                    showHeader("Activate/Deactivate Student");
                    
                    int studentId;
                    
                    printf("Enter student ID: ");
                    scanf("%d", &studentId);
                    getchar(); // Clear input buffer
                    
                    snprintf(request, BUFFER_SIZE, "ADMIN %d TOGGLE_STUDENT %d", userId, studentId);
                    communicateWithServer(request, response);
                    
                    showSuccessMessage(response);
                    break;
                }
                case 4: { // Update User details
                    clearConsole();
                    showHeader("Update User Details");
                    
                    int targetUserId;
                    char field[20];
                    char value[PASSWORD_MAX_LENGTH];
                    
                    printf("Enter user ID: ");
                    scanf("%d", &targetUserId);
                    getchar(); // Clear input buffer
                    
                    printf("What to update (username/password): ");
                    scanf("%s", field);
                    getchar(); // Clear input buffer
                    
                    if (strcmp(field, "password") == 0) {
                        printf("Enter new password: ");
                        securePasswordInput(value);
                    } else {
                        printf("Enter new value: ");
                        scanf("%s", value);
                        getchar(); // Clear input buffer
                    }
                    
                    snprintf(request, BUFFER_SIZE, "ADMIN %d UPDATE_USER %d %s %s", 
                            userId, targetUserId, field, value);
                    communicateWithServer(request, response);
                    
                    showSuccessMessage(response);
                    break;
                }
                case 5: { // View All Users
                    clearConsole();
                    showHeader("All Users");
                    
                    snprintf(request, BUFFER_SIZE, "ADMIN %d VIEW_USERS", userId);
                    communicateWithServer(request, response);
                    
                    printf("%s\n", response);
                    pauseExecution();
                    break;
                }
                case 6: { // View All Courses
                    clearConsole();
                    showHeader("All Courses");
                    
                    snprintf(request, BUFFER_SIZE, "ADMIN %d VIEW_COURSES", userId);
                    communicateWithServer(request, response);
                    
                    printf("%s\n", response);
                    pauseExecution();
                    break;
                }
                case 7: { // Logout
                    loggedIn = false;
                    strcpy(userRole, "");
                    userId = -1;
                    printf("Logged out successfully.\n");
                    pauseExecution();
                    return; // Return to login screen
                }
                case 8: { // Exit
                    communicateWithServer("EXIT", response);
                    gracefulExit(0);
                    break;
                }
                default:
                    showErrorMessage("Invalid choice!");
            }
        }
    }
    
    // Student interface
    void displayStudentInterface() {
        while (loggedIn) {
            clearConsole();
            showHeader("Student Dashboard");
            
            printf("1. Enroll to new Courses\n");
            printf("2. Unenroll from already enrolled Courses\n");
            printf("3. View enrolled Courses\n");
            printf("4. View all available Courses\n");
            printf("5. Change Password\n");
            printf("6. Logout\n");
            printf("7. Exit\n");
            
            printf("\nEnter your choice: ");
            
            int choice;
            scanf("%d", &choice);
            getchar(); // Clear input buffer
            
            char request[BUFFER_SIZE];
            char response[BUFFER_SIZE];
            
            switch (choice) {
                case 1: { // Enroll to new course
                    clearConsole();
                    showHeader("Enroll to New Course");
                    
                    // First, show available courses
                    snprintf(request, BUFFER_SIZE, "STUDENT %d VIEW_COURSES", userId);
                    communicateWithServer(request, response);
                    printf("%s\n", response);
                    
                    char courseCode[20];
                    printf("\nEnter course code to enroll: ");
                    scanf("%s", courseCode);
                    getchar(); // Clear input buffer
                    
                    snprintf(request, BUFFER_SIZE, "STUDENT %d ENROLL %s", userId, courseCode);
                    communicateWithServer(request, response);
                    
                    showSuccessMessage(response);
                    break;
                }
                case 2: { // Unenroll from course
                    clearConsole();
                    showHeader("Unenroll from Course");
                    
                    // First, show enrolled courses
                    snprintf(request, BUFFER_SIZE, "STUDENT %d VIEW_ENROLLED", userId);
                    communicateWithServer(request, response);
                    printf("%s\n", response);
                    
                    char courseCode[20];
                    printf("\nEnter course code to unenroll: ");
                    scanf("%s", courseCode);
                    getchar(); // Clear input buffer
                    
                    snprintf(request, BUFFER_SIZE, "STUDENT %d UNENROLL %s", userId, courseCode);
                    communicateWithServer(request, response);
                    
                    showSuccessMessage(response);
                    break;
                }
                case 3: { // View enrolled courses
                    clearConsole();
                    showHeader("Your Enrolled Courses");
                    
                    snprintf(request, BUFFER_SIZE, "STUDENT %d VIEW_ENROLLED", userId);
                    communicateWithServer(request, response);
                    
                    printf("%s\n", response);
                    pauseExecution();
                    break;
                }
                case 4: { // View all available courses
                    clearConsole();
                    showHeader("All Available Courses");
                    
                    snprintf(request, BUFFER_SIZE, "STUDENT %d VIEW_COURSES", userId);
                    communicateWithServer(request, response);
                    
                    printf("%s\n", response);
                    pauseExecution();
                    break;
                }
                case 5: { // Change password
                    clearConsole();
                    showHeader("Change Password");
                    
                    char oldPassword[PASSWORD_MAX_LENGTH];
                    char newPassword[PASSWORD_MAX_LENGTH];
                    
                    printf("Enter current password: ");
                    securePasswordInput(oldPassword);
                    
                    printf("\nEnter new password: ");
                    securePasswordInput(newPassword);
                    
                    snprintf(request, BUFFER_SIZE, "STUDENT %d CHANGE_PASSWORD %s %s", 
                            userId, oldPassword, newPassword);
                    communicateWithServer(request, response);
                    
                    showSuccessMessage(response);
                    break;
                }
                case 6: { // Logout
                    loggedIn = false;
                    strcpy(userRole, "");
                    userId = -1;
                    printf("Logged out successfully.\n");
                    pauseExecution();
                    return; // Return to login screen
                }
                case 7: { // Exit
                    communicateWithServer("EXIT", response);
                    gracefulExit(0);
                    break;
                }
                default:
                    showErrorMessage("Invalid choice!");
            }
        }
    }
    
    // Faculty interface
    void displayFacultyInterface() {
        while (loggedIn) {
            clearConsole();
            showHeader("Faculty Dashboard");
            
            printf("1. Add new Course\n");
            printf("2. Remove offered Course\n");
            printf("3. View enrollments in Courses\n");
            printf("4. View your Courses\n");
            printf("5. Change Password\n");
            printf("6. Logout\n");
            printf("7. Exit\n");
            
            printf("\nEnter your choice: ");
            
            int choice;
            scanf("%d", &choice);
            getchar(); // Clear input buffer
            
            char request[BUFFER_SIZE];
            char response[BUFFER_SIZE];
            
            switch (choice) {
                case 1: { // Add new course
                    clearConsole();
                    showHeader("Add New Course");
                    
                    char courseCode[20];
                    char courseName[COURSE_NAME_MAX_LENGTH];
                    int seats;
                    
                    printf("Enter course code: ");
                    scanf("%s", courseCode);
                    getchar(); // Clear input buffer
                    
                    printf("Enter total seats: ");
                    scanf("%d", &seats);
                    getchar(); // Clear input buffer
                    
                    printf("Enter course name: ");
                    fgets(courseName, COURSE_NAME_MAX_LENGTH, stdin);
                    // Remove trailing newline
                    size_t len = strlen(courseName);
                    if (len > 0 && courseName[len-1] == '\n') {
                        courseName[len-1] = '\0';
                    }
                    
                    snprintf(request, BUFFER_SIZE, "FACULTY %d ADD_COURSE %s %d %s", 
                            userId, courseCode, seats, courseName);
                    communicateWithServer(request, response);
                    
                    showSuccessMessage(response);
                    break;
                }
                case 2: { // Remove offered course
                    clearConsole();
                    showHeader("Remove Course");
                    
                    // First, show faculty's courses
                    snprintf(request, BUFFER_SIZE, "FACULTY %d VIEW_COURSES", userId);
                    communicateWithServer(request, response);
                    printf("%s\n", response);
                    
                    char courseCode[20];
                    printf("\nEnter course code to remove: ");
                    scanf("%s", courseCode);
                    getchar(); // Clear input buffer
                    
                    snprintf(request, BUFFER_SIZE, "FACULTY %d REMOVE_COURSE %s", userId, courseCode);
                    communicateWithServer(request, response);
                
                    showSuccessMessage(response);
                    break;
                }
                case 3: { // View enrollments in courses
                    clearConsole();
                    showHeader("Course Enrollments");
                    
                    snprintf(request, BUFFER_SIZE, "FACULTY %d VIEW_ENROLLMENTS", userId);
                    communicateWithServer(request, response);
                    
                    printf("%s\n", response);
                    pauseExecution();
                    break;
                }
                case 4: { // View faculty's courses
                    clearConsole();
                    showHeader("Your Courses");
                    
                    snprintf(request, BUFFER_SIZE, "FACULTY %d VIEW_COURSES", userId);
                    communicateWithServer(request, response);
                    
                    printf("%s\n", response);
                    pauseExecution();
                    break;
                }
                case 5: { // Change password
                    clearConsole();
                    showHeader("Change Password");
                    
                    char oldPassword[PASSWORD_MAX_LENGTH];
                    char newPassword[PASSWORD_MAX_LENGTH];
                    
                    printf("Enter current password: ");
                    securePasswordInput(oldPassword);
                    
                    printf("\nEnter new password: ");
                    securePasswordInput(newPassword);
                    
                    snprintf(request, BUFFER_SIZE, "FACULTY %d CHANGE_PASSWORD %s %s", 
                            userId, oldPassword, newPassword);
                    communicateWithServer(request, response);
                    
                    showSuccessMessage(response);
                    break;
                }
                case 6: { // Logout
                    loggedIn = false;
                    strcpy(userRole, "");
                    userId = -1;
                    printf("Logged out successfully.\n");
                    pauseExecution();
                    return; // Return to login screen
                }
                case 7: { // Exit
                    communicateWithServer("EXIT", response);
                    gracefulExit(0);
                    break;
                }
                default:
                    showErrorMessage("Invalid choice!");
            }
        }
    }
    
    // Main function
    int main() {
        // Set up signal handler
        signal(SIGINT, gracefulExit);
        
        // Connect to server
        setupConnection();
        
        // Start with login screen
        while (true) {
            displayLoginScreen();
        }
        
        return 0;
    }