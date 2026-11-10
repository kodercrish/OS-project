/**
 * Krish Patel
 * IMT2023134
 * 12th May, 2025
 */

// Academia Portal Server
// CS-513 System Software Mini Project

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <semaphore.h>
#include <asm-generic/socket.h>

#define SERVER_PORT 8080
#define CLIENT_MAX 100
#define MSG_BUFFER_SIZE 1024
#define COURSE_MAX_CAPACITY 10
#define USER_MAX_COUNT 1000
#define COURSE_MAX_COUNT 1000
#define ENROLLMENT_MAX_COUNT 10000
#define STRING_MAX_LENGTH 256

// Data structures
enum UserRole {
    ADMIN = 1,
    STUDENT = 2,
    FACULTY = 3
};

typedef struct {
    int id;
    char username[STRING_MAX_LENGTH];
    char password[STRING_MAX_LENGTH];
    enum UserRole role;
    int isActive; // 1 for true, 0 for false
} User;

typedef struct {
    int id;
    char code[STRING_MAX_LENGTH];
    char name[STRING_MAX_LENGTH];
    int facultyId;
    int maxSeats;
    int currentEnrollment;
} Course;

typedef struct {
    int studentId;
    int courseId;
} Enrollment;

// Global variables
sem_t accessMutex;
User* userDatabase = NULL;
int userCount = 0;
Course* courseDatabase = NULL;
int courseCount = 0;
Enrollment* enrollmentDatabase = NULL;
int enrollmentCount = 0;

// File paths
const char* USER_DB_FILE = "users.txt";
const char* COURSE_DB_FILE = "courses.txt";
const char* ENROLLMENT_DB_FILE = "enrollments.txt";

// Forward declarations
void initializeDatabase();
void persistDatabase();
char* customStrdup(const char* s);
void handleSignal(int signal_num);
void acquireFileLock(int lockType, const char* filename);
void releaseFileLock(const char* filename);

// User management functions
User* getUserById(int id);
User* getUserByUsername(const char* username);
char* authenticateUser(const char* username, const char* password);

// Course management functions
Course* getCourseById(int id);
Course* getCourseByCode(const char* code);
int checkEnrollmentStatus(int studentId, int courseId);

// Request handlers
char* processClientRequest(const char* request, int clientSocket);
char* handleAdminCommands(const char* request, int userId);
char* handleStudentCommands(const char* request, int userId);
char* handleFacultyCommands(const char* request, int userId);
void* clientConnectionHandler(void* client_socket);

// File locking utilities
void acquireReadLock(const char* filename) {
    acquireFileLock(LOCK_SH, filename);
}

void acquireWriteLock(const char* filename) {
    acquireFileLock(LOCK_EX, filename);
}

void acquireFileLock(int lockType, const char* filename) {
    int fd = open(filename, O_RDONLY | O_CREAT, 0644);
    if (fd != -1) {
        flock(fd, lockType);
        close(fd);
    }
}

void releaseFileLock(const char* filename) {
    int fd = open(filename, O_RDONLY);
    if (fd != -1) {
        flock(fd, LOCK_UN);
        close(fd);
    }
}

// Custom string duplication for systems lacking strdup
char* customStrdup(const char* s) {
    size_t len = strlen(s) + 1;
    char* newStr = (char*)malloc(len);
    if (newStr) {
        memcpy(newStr, s, len);
    }
    return newStr;
}

// Signal handler for graceful shutdown
void handleSignal(int signal_num) {
    printf("\nReceived signal %d. Performing cleanup before exit...\n", signal_num);
    persistDatabase();
    sem_destroy(&accessMutex);
    
    if (userDatabase) free(userDatabase);
    if (courseDatabase) free(courseDatabase);
    if (enrollmentDatabase) free(enrollmentDatabase);
    
    exit(signal_num);
}

// Database initialization
void initializeDatabase() {
    FILE* file;
    char buffer[MSG_BUFFER_SIZE];
    
    // Load user data
    file = fopen(USER_DB_FILE, "r");
    if (file) {
        while (fgets(buffer, sizeof(buffer), file)) {
            User user;
            char roleStr[20];
            int active;
            
            if (sscanf(buffer, "%d %s %s %s %d", &user.id, user.username, 
                      user.password, roleStr, &active) == 5) {
                
                if (strcmp(roleStr, "ADMIN") == 0) user.role = ADMIN;
                else if (strcmp(roleStr, "STUDENT") == 0) user.role = STUDENT;
                else if (strcmp(roleStr, "FACULTY") == 0) user.role = FACULTY;
                
                user.isActive = active;
                
                userDatabase = (User*)realloc(userDatabase, (userCount + 1) * sizeof(User));
                userDatabase[userCount++] = user;
            }
        }
        fclose(file);
    } else {
        // Create default admin if no user file exists
        User admin = {
            .id = 1,
            .username = "admin",
            .password = "admin123",
            .role = ADMIN,
            .isActive = 1
        };
        
        userDatabase = (User*)malloc(sizeof(User));
        userDatabase[0] = admin;
        userCount = 1;
        
        file = fopen(USER_DB_FILE, "w");
        fprintf(file, "%d %s %s ADMIN %d\n", admin.id, admin.username, admin.password, admin.isActive);
        fclose(file);
    }

    // Load course data
    file = fopen(COURSE_DB_FILE, "r");
    if (file) {
        while (fgets(buffer, sizeof(buffer), file)) {
            Course course;
            char tempName[MSG_BUFFER_SIZE];
            
            if (sscanf(buffer, "%d %s %d %d %d %[^\n]", &course.id, course.code, 
                      &course.facultyId, &course.maxSeats, &course.currentEnrollment, 
                      tempName) >= 5) {
                
                strcpy(course.name, tempName);
                courseDatabase = (Course*)realloc(courseDatabase, (courseCount + 1) * sizeof(Course));
                courseDatabase[courseCount++] = course;
            }
        }
        fclose(file);
    }

    // Load enrollment data
    file = fopen(ENROLLMENT_DB_FILE, "r");
    if (file) {
        while (fgets(buffer, sizeof(buffer), file)) {
            Enrollment enrollment;
            
            if (sscanf(buffer, "%d %d", &enrollment.studentId, &enrollment.courseId) == 2) {
                enrollmentDatabase = (Enrollment*)realloc(enrollmentDatabase, 
                                    (enrollmentCount + 1) * sizeof(Enrollment));
                enrollmentDatabase[enrollmentCount++] = enrollment;
            }
        }
        fclose(file);
    }
}

// Save database to files
void persistDatabase() {
    sem_wait(&accessMutex);

    // Save users
    FILE* userFile = fopen(USER_DB_FILE, "w");
    for (int i = 0; i < userCount; i++) {
        const char* roleStr = userDatabase[i].role == ADMIN ? "ADMIN" : 
                            userDatabase[i].role == STUDENT ? "STUDENT" : "FACULTY";
        fprintf(userFile, "%d %s %s %s %d\n", userDatabase[i].id, userDatabase[i].username, 
                userDatabase[i].password, roleStr, userDatabase[i].isActive);
    }
    fclose(userFile);

    // Save courses
    FILE* courseFile = fopen(COURSE_DB_FILE, "w");
    for (int i = 0; i < courseCount; i++) {
        fprintf(courseFile, "%d %s %d %d %d %s\n", courseDatabase[i].id, courseDatabase[i].code, 
                courseDatabase[i].facultyId, courseDatabase[i].maxSeats, 
                courseDatabase[i].currentEnrollment, courseDatabase[i].name);
    }
    fclose(courseFile);

    // Save enrollments
    FILE* enrollmentFile = fopen(ENROLLMENT_DB_FILE, "w");
    for (int i = 0; i < enrollmentCount; i++) {
        fprintf(enrollmentFile, "%d %d\n", enrollmentDatabase[i].studentId, 
                enrollmentDatabase[i].courseId);
    }
    fclose(enrollmentFile);

    sem_post(&accessMutex);
}

// User lookup functions
User* getUserById(int id) {
    for (int i = 0; i < userCount; i++) {
        if (userDatabase[i].id == id) {
            return &userDatabase[i];
        }
    }
    return NULL;
}

User* getUserByUsername(const char* username) {
    for (int i = 0; i < userCount; i++) {
        if (strcmp(userDatabase[i].username, username) == 0) {
            return &userDatabase[i];
        }
    }
    return NULL;
}

// Course lookup functions
Course* getCourseById(int id) {
    for (int i = 0; i < courseCount; i++) {
        if (courseDatabase[i].id == id) {
            return &courseDatabase[i];
        }
    }
    return NULL;
}

Course* getCourseByCode(const char* code) {
    for (int i = 0; i < courseCount; i++) {
        if (strcmp(courseDatabase[i].code, code) == 0) {
            return &courseDatabase[i];
        }
    }
    return NULL;
}

// Check if student is enrolled in a course
int checkEnrollmentStatus(int studentId, int courseId) {
    for (int i = 0; i < enrollmentCount; i++) {
        if (enrollmentDatabase[i].studentId == studentId && 
            enrollmentDatabase[i].courseId == courseId) {
            return 1;
        }
    }
    return 0;
}

// User authentication
char* authenticateUser(const char* username, const char* password) {
    char* response = (char*)malloc(MSG_BUFFER_SIZE);
    
    for (int i = 0; i < userCount; i++) {
        if (strcmp(userDatabase[i].username, username) == 0 && 
            strcmp(userDatabase[i].password, password) == 0) {
            
            if (!userDatabase[i].isActive) {
                strcpy(response, "LOGIN_FAILED Account deactivated");
                return response;
            }
            
            const char* roleStr = userDatabase[i].role == ADMIN ? "ADMIN" : 
                                userDatabase[i].role == STUDENT ? "STUDENT" : "FACULTY";
            
            sprintf(response, "LOGIN_SUCCESS %s %d", roleStr, userDatabase[i].id);
            return response;
        }
    }
    
    strcpy(response, "LOGIN_FAILED Invalid credentials");
    return response;
}

// Process client requests
char* processClientRequest(const char* request, int clientSocket) {
    char* response = (char*)malloc(MSG_BUFFER_SIZE);
    response[0] = '\0';

    // Create a copy of the request for tokenization
    char requestCopy[MSG_BUFFER_SIZE];
    strncpy(requestCopy, request, MSG_BUFFER_SIZE - 1);
    requestCopy[MSG_BUFFER_SIZE - 1] = '\0';

    char* token = strtok(requestCopy, " ");
    if (!token) {
        strcpy(response, "Invalid request format");
        return response;
    }

    char command[50];
    strcpy(command, token);

    // Handle login request
    if (strcmp(command, "LOGIN") == 0) {
        char* username = strtok(NULL, " ");
        char* password = strtok(NULL, " ");
        
        if (username && password) {
            strcpy(response, authenticateUser(username, password));
        } else {
            strcpy(response, "Invalid login format");
        }
    }
    // Handle role-specific requests
    else if (strcmp(command, "ADMIN") == 0 || 
             strcmp(command, "STUDENT") == 0 || 
             strcmp(command, "FACULTY") == 0) {
        
        char* userIdStr = strtok(NULL, " ");
        if (!userIdStr) {
            strcpy(response, "Invalid request format");
            return response;
        }
        
        int userId = atoi(userIdStr);
        char* subRequest = strtok(NULL, "");
        if (!subRequest) subRequest = "";

        if (strcmp(command, "ADMIN") == 0) {
            strcpy(response, handleAdminCommands(subRequest, userId));
        } else if (strcmp(command, "STUDENT") == 0) {
            strcpy(response, handleStudentCommands(subRequest, userId));
        } else {
            strcpy(response, handleFacultyCommands(subRequest, userId));
        }
    } else {
        strcpy(response, "Invalid request format");
    }

    return response;
}

// Handle admin commands
char* handleAdminCommands(const char* request, int userId) {
    char* response = (char*)malloc(MSG_BUFFER_SIZE);
    User* admin = getUserById(userId);
    
    if (!admin || admin->role != ADMIN) {
        strcpy(response, "Access denied");
        return response;
    }

    // Create a copy of the request for tokenization
    char requestCopy[MSG_BUFFER_SIZE];
    strncpy(requestCopy, request, MSG_BUFFER_SIZE - 1);
    requestCopy[MSG_BUFFER_SIZE - 1] = '\0';

    char* token = strtok(requestCopy, " ");
    if (!token) {
        strcpy(response, "Invalid admin request");
        return response;
    }

    char command[50];
    strcpy(command, token);

    if (strcmp(command, "ADD_STUDENT") == 0) {
        char* username = strtok(NULL, " ");
        char* password = strtok(NULL, " ");
        
        if (!username || !password) {
            strcpy(response, "Invalid format");
            return response;
        }
        
        if (getUserByUsername(username)) {
            sprintf(response, "Student with username %s already exists", username);
            return response;
        }
        
        User student;
        student.id = userCount ? userDatabase[userCount-1].id + 1 : 1;
        strncpy(student.username, username, STRING_MAX_LENGTH-1);
        student.username[STRING_MAX_LENGTH-1] = '\0';
        strncpy(student.password, password, STRING_MAX_LENGTH-1);
        student.password[STRING_MAX_LENGTH-1] = '\0';
        student.role = STUDENT;
        student.isActive = 1;
        
        userDatabase = (User*)realloc(userDatabase, (userCount + 1) * sizeof(User));
        userDatabase[userCount++] = student;
        persistDatabase();
        
        sprintf(response, "Student added successfully with ID %d", student.id);
    }
    else if (strcmp(command, "ADD_FACULTY") == 0) {
        char* username = strtok(NULL, " ");
        char* password = strtok(NULL, " ");
        
        if (!username || !password) {
            strcpy(response, "Invalid format");
            return response;
        }
        
        if (getUserByUsername(username)) {
            sprintf(response, "Faculty with username %s already exists", username);
            return response;
        }
        
        User faculty;
        faculty.id = userCount ? userDatabase[userCount-1].id + 1 : 1;
        strncpy(faculty.username, username, STRING_MAX_LENGTH-1);
        faculty.username[STRING_MAX_LENGTH-1] = '\0';
        strncpy(faculty.password, password, STRING_MAX_LENGTH-1);
        faculty.password[STRING_MAX_LENGTH-1] = '\0';
        faculty.role = FACULTY;
        faculty.isActive = 1;
        
        userDatabase = (User*)realloc(userDatabase, (userCount + 1) * sizeof(User));
        userDatabase[userCount++] = faculty;
        persistDatabase();
        
        sprintf(response, "Faculty added successfully with ID %d", faculty.id);
    }
    else if (strcmp(command, "TOGGLE_STUDENT") == 0) {
        char* studentIdStr = strtok(NULL, " ");
        
        if (!studentIdStr) {
            strcpy(response, "Invalid format");
            return response;
        }
        
        int studentId = atoi(studentIdStr);
        User* student = getUserById(studentId);
        
        if (!student || student->role != STUDENT) {
            strcpy(response, "Student not found");
            return response;
        }
        
        student->isActive = !student->isActive;
        persistDatabase();
        
        sprintf(response, "Student %s %s successfully", student->username, 
                student->isActive ? "activated" : "deactivated");
    }
    else if (strcmp(command, "UPDATE_USER") == 0) {
        char* userIdStr = strtok(NULL, " ");
        char* field = strtok(NULL, " ");
        char* value = strtok(NULL, " ");
        
        if (!userIdStr || !field || !value) {
            strcpy(response, "Invalid format");
            return response;
        }
        
        int userId = atoi(userIdStr);
        User* user = getUserById(userId);
        
        if (!user) {
            strcpy(response, "User not found");
            return response;
        }
        
        if (strcmp(field, "password") == 0) {
            strncpy(user->password, value, STRING_MAX_LENGTH-1);
            user->password[STRING_MAX_LENGTH-1] = '\0';
            persistDatabase();
            strcpy(response, "Password updated successfully");
        }
        else if (strcmp(field, "username") == 0) {
            if (getUserByUsername(value)) {
                sprintf(response, "Username %s already exists", value);
            } else {
                strncpy(user->username, value, STRING_MAX_LENGTH-1);
                user->username[STRING_MAX_LENGTH-1] = '\0';
                persistDatabase();
                strcpy(response, "Username updated successfully");
            }
        }
        else {
            strcpy(response, "Invalid field to update");
        }
    }
    else if (strcmp(command, "VIEW_USERS") == 0) {
        char* result = (char*)malloc(MSG_BUFFER_SIZE);
        strcpy(result, "Users list:\n");
        
        for (int i = 0; i < userCount; i++) {
            const char* roleStr = userDatabase[i].role == ADMIN ? "ADMIN" : 
                                userDatabase[i].role == STUDENT ? "STUDENT" : "FACULTY";
            
            char line[256];
            sprintf(line, "ID: %d, Username: %s, Type: %s, Status: %s\n", 
                    userDatabase[i].id, userDatabase[i].username, roleStr, 
                    userDatabase[i].isActive ? "Active" : "Inactive");
            
            strncat(result, line, MSG_BUFFER_SIZE - strlen(result) - 1);
        }
        
        strcpy(response, result);
        free(result);
    }
    else if (strcmp(command, "VIEW_COURSES") == 0) {
        acquireReadLock(COURSE_DB_FILE);
        
        char* result = (char*)malloc(MSG_BUFFER_SIZE);
        strcpy(result, "Courses list:\n");
        
        for (int i = 0; i < courseCount; i++) {
            User* faculty = getUserById(courseDatabase[i].facultyId);
            
            char line[256];
            sprintf(line, "ID: %d, Code: %s, Name: %s, Faculty: %s, Seats: %d/%d\n", 
                    courseDatabase[i].id, courseDatabase[i].code, courseDatabase[i].name, 
                    faculty ? faculty->username : "Unknown", 
                    courseDatabase[i].currentEnrollment, courseDatabase[i].maxSeats);
            
            strncat(result, line, MSG_BUFFER_SIZE - strlen(result) - 1);
        }
        
        releaseFileLock(COURSE_DB_FILE);
        strcpy(response, result);
        free(result);
    }
    else {
        strcpy(response, "Invalid admin command");
    }

    return response;
}

// Handle student commands
char* handleStudentCommands(const char* request, int userId) {
    char* response = (char*)malloc(MSG_BUFFER_SIZE);
    User* student = getUserById(userId);
    
    if (!student || student->role != STUDENT) {
        strcpy(response, "Access denied");
        return response;
    }

    // Create a copy of the request for tokenization
    char requestCopy[MSG_BUFFER_SIZE];
    strncpy(requestCopy, request, MSG_BUFFER_SIZE - 1);
    requestCopy[MSG_BUFFER_SIZE - 1] = '\0';

    char* token = strtok(requestCopy, " ");
    if (!token) {
        strcpy(response, "Invalid student request");
        return response;
    }

    char command[50];
    strcpy(command, token);

    if (strcmp(command, "ENROLL") == 0) {
        char* courseCode = strtok(NULL, " ");
        
        if (!courseCode) {
            strcpy(response, "Invalid format");
            return response;
        }
        
        acquireWriteLock(COURSE_DB_FILE);
        acquireWriteLock(ENROLLMENT_DB_FILE);
        
        Course* course = getCourseByCode(courseCode);
        
        if (!course) {
            releaseFileLock(COURSE_DB_FILE);
            releaseFileLock(ENROLLMENT_DB_FILE);
            strcpy(response, "Course not found");
            return response;
        }
        
        if (checkEnrollmentStatus(student->id, course->id)) {
            releaseFileLock(COURSE_DB_FILE);
            releaseFileLock(ENROLLMENT_DB_FILE);
            strcpy(response, "Already enrolled in this course");
            return response;
        }

        sleep(30);
        
        if (course->currentEnrollment >= course->maxSeats) {
            releaseFileLock(COURSE_DB_FILE);
            releaseFileLock(ENROLLMENT_DB_FILE);
            strcpy(response, "Course is full");
            return response;
        }
        
        Enrollment enrollment;
        enrollment.studentId = student->id;
        enrollment.courseId = course->id;
        
        enrollmentDatabase = (Enrollment*)realloc(enrollmentDatabase, 
                            (enrollmentCount + 1) * sizeof(Enrollment));
        enrollmentDatabase[enrollmentCount++] = enrollment;
        
        course->currentEnrollment++;
        persistDatabase();
        
        releaseFileLock(COURSE_DB_FILE);
        releaseFileLock(ENROLLMENT_DB_FILE);
        
        sprintf(response, "Successfully enrolled in %s - %s", course->code, course->name);
    }
    else if (strcmp(command, "UNENROLL") == 0) {
        char* courseCode = strtok(NULL, " ");
        
        if (!courseCode) {
            strcpy(response, "Invalid format");
            return response;
        }
        
        acquireWriteLock(COURSE_DB_FILE);
        acquireWriteLock(ENROLLMENT_DB_FILE);
        
        Course* course = getCourseByCode(courseCode);
        
        if (!course) {
            releaseFileLock(COURSE_DB_FILE);
            releaseFileLock(ENROLLMENT_DB_FILE);
            strcpy(response, "Course not found");
            return response;
        }
        
        int found = 0;
        for (int i = 0; i < enrollmentCount; i++) {
            if (enrollmentDatabase[i].studentId == student->id && 
                enrollmentDatabase[i].courseId == course->id) {
                
                // Remove this enrollment by shifting remaining elements
                for (int j = i; j < enrollmentCount-1; j++) {
                    enrollmentDatabase[j] = enrollmentDatabase[j+1];
                }
                
                enrollmentCount--;
                enrollmentDatabase = (Enrollment*)realloc(enrollmentDatabase, 
                                    enrollmentCount * sizeof(Enrollment));
                found = 1;
                break;
            }
        }
        
        if (!found) {
            releaseFileLock(COURSE_DB_FILE);
            releaseFileLock(ENROLLMENT_DB_FILE);
            strcpy(response, "Not enrolled in this course");
            return response;
        }
        
        course->currentEnrollment--;
        persistDatabase();
        
        releaseFileLock(COURSE_DB_FILE);
        releaseFileLock(ENROLLMENT_DB_FILE);
        
        sprintf(response, "Successfully unenrolled from %s - %s", course->code, course->name);
    }
    else if (strcmp(command, "VIEW_ENROLLED") == 0) {
        acquireReadLock(COURSE_DB_FILE);
        acquireReadLock(ENROLLMENT_DB_FILE);
        
        char* result = (char*)malloc(MSG_BUFFER_SIZE);
        strcpy(result, "Enrolled courses:\n");
        
        int hasEnrollments = 0;
        for (int i = 0; i < enrollmentCount; i++) {
            if (enrollmentDatabase[i].studentId == student->id) {
                Course* course = getCourseById(enrollmentDatabase[i].courseId);
                
                if (course) {
                    char line[256];
                    sprintf(line, "Code: %s, Name: %s\n", course->code, course->name);
                    strncat(result, line, MSG_BUFFER_SIZE - strlen(result) - 1);
                    hasEnrollments = 1;
                }
            }
        }
        
        releaseFileLock(COURSE_DB_FILE);
        releaseFileLock(ENROLLMENT_DB_FILE);
        
        if (!hasEnrollments) {
            strcpy(response, "You are not enrolled in any courses");
        } else {
            strcpy(response, result);
        }
        
        free(result);
    }
    else if (strcmp(command, "VIEW_COURSES") == 0) {
        acquireReadLock(COURSE_DB_FILE);
        
        char* result = (char*)malloc(MSG_BUFFER_SIZE);
        strcpy(result, "Available courses:\n");
        
        for (int i = 0; i < courseCount; i++) {
            User* faculty = getUserById(courseDatabase[i].facultyId);
            
            char line[256];
            sprintf(line, "Code: %s, Name: %s, Faculty: %s, Available seats: %d/%d\n", 
                    courseDatabase[i].code, courseDatabase[i].name, 
                    faculty ? faculty->username : "Unknown", 
                    courseDatabase[i].maxSeats - courseDatabase[i].currentEnrollment, 
                    courseDatabase[i].maxSeats);
            
            strncat(result, line, MSG_BUFFER_SIZE - strlen(result) - 1);
        }
        
        releaseFileLock(COURSE_DB_FILE);
        strcpy(response, result);
        free(result);
    }
    else if (strcmp(command, "CHANGE_PASSWORD") == 0) {
        char* oldPassword = strtok(NULL, " ");
        char* newPassword = strtok(NULL, " ");
        
        if (!oldPassword || !newPassword) {
            strcpy(response, "Invalid format");
            return response;
        }
        
        if (strcmp(student->password, oldPassword) != 0) {
            strcpy(response, "Incorrect current password");
            return response;
        }
        
        strncpy(student->password, newPassword, STRING_MAX_LENGTH-1);
        student->password[STRING_MAX_LENGTH-1] = '\0';
        persistDatabase();
        
        strcpy(response, "Password changed successfully");
    }
    else {
        strcpy(response, "Invalid student command");
    }

    return response;
}

// Handle faculty commands
char* handleFacultyCommands(const char* request, int userId) {
    char* response = (char*)malloc(MSG_BUFFER_SIZE);
    User* faculty = getUserById(userId);
    
    if (!faculty || faculty->role != FACULTY) {
        strcpy(response, "Access denied");
        return response;
    }

    // Create a copy of the request for tokenization
    char requestCopy[MSG_BUFFER_SIZE];
    strncpy(requestCopy, request, MSG_BUFFER_SIZE - 1);
    requestCopy[MSG_BUFFER_SIZE - 1] = '\0';

    char* token = strtok(requestCopy, " ");
    if (!token) {
        strcpy(response, "Invalid faculty request");
        return response;
    }

    char command[50];
    strcpy(command, token);

    if (strcmp(command, "ADD_COURSE") == 0) {
        char* courseCode = strtok(NULL, " ");
        char* seatsStr = strtok(NULL, " ");
        char* courseName = strtok(NULL, "");
        
        if (!courseCode || !seatsStr || !courseName) {
            strcpy(response, "Invalid format");
            return response;
        }
        
        int seats = atoi(seatsStr);
        acquireWriteLock(COURSE_DB_FILE);
        
        for (int i = 0; i < courseCount; i++) {
            if (strcmp(courseDatabase[i].code, courseCode) == 0) {
                releaseFileLock(COURSE_DB_FILE);
                sprintf(response, "Course with code %s already exists", courseCode);
                return response;
            }
        }
        
        Course course;
        course.id = courseCount ? courseDatabase[courseCount-1].id + 1 : 1;
        strncpy(course.code, courseCode, STRING_MAX_LENGTH-1);
        course.code[STRING_MAX_LENGTH-1] = '\0';
        strncpy(course.name, courseName, STRING_MAX_LENGTH-1);
        course.name[STRING_MAX_LENGTH-1] = '\0';
        course.facultyId = faculty->id;
        course.maxSeats = seats;
        course.currentEnrollment = 0;
        
        courseDatabase = (Course*)realloc(courseDatabase, (courseCount + 1) * sizeof(Course));
        courseDatabase[courseCount++] = course;
        persistDatabase();
        
        releaseFileLock(COURSE_DB_FILE);
        sprintf(response, "Course added successfully: %s - %s", courseCode, courseName);
    }
    else if (strcmp(command, "REMOVE_COURSE") == 0) {
        char* courseCode = strtok(NULL, " ");
        
        if (!courseCode) {
            strcpy(response, "Invalid format");
            return response;
        }
        
        acquireWriteLock(COURSE_DB_FILE);
        acquireWriteLock(ENROLLMENT_DB_FILE);
        
        int courseId = -1;
        for (int i = 0; i < courseCount; i++) {
            if (strcmp(courseDatabase[i].code, courseCode) == 0 && 
                courseDatabase[i].facultyId == faculty->id) {
                
                courseId = courseDatabase[i].id;
                
                // Remove course by shifting remaining elements
                for (int j = i; j < courseCount-1; j++) {
                    courseDatabase[j] = courseDatabase[j+1];
                }
                
                courseCount--;
                courseDatabase = (Course*)realloc(courseDatabase, courseCount * sizeof(Course));
                break;
            }
        }
        
        if (courseId == -1) {
            releaseFileLock(COURSE_DB_FILE);
            releaseFileLock(ENROLLMENT_DB_FILE);
            strcpy(response, "Course not found or you don't have permission to remove it");
            return response;
        }
        
        // Remove all enrollments for this course
        int newSize = 0;
        for (int i = 0; i < enrollmentCount; i++) {
            if (enrollmentDatabase[i].courseId != courseId) {
                enrollmentDatabase[newSize] = enrollmentDatabase[i];
                newSize++;
            }
        }
        
        enrollmentCount = newSize;
        enrollmentDatabase = (Enrollment*)realloc(enrollmentDatabase, 
                            enrollmentCount * sizeof(Enrollment));
        
        persistDatabase();
        releaseFileLock(COURSE_DB_FILE);
        releaseFileLock(ENROLLMENT_DB_FILE);
        
        sprintf(response, "Course %s removed successfully", courseCode);
    }
    else if (strcmp(command, "VIEW_ENROLLMENTS") == 0) {
        acquireReadLock(COURSE_DB_FILE);
        acquireReadLock(ENROLLMENT_DB_FILE);
        
        char* result = (char*)malloc(MSG_BUFFER_SIZE);
        strcpy(result, "Course enrollments:\n");
        
        int hasCourses = 0;
        for (int i = 0; i < courseCount; i++) {
            if (courseDatabase[i].facultyId == faculty->id) {
                char line[256];
                sprintf(line, "\nCourse: %s - %s\nEnrolled students: %d/%d\n", 
                        courseDatabase[i].code, courseDatabase[i].name, 
                        courseDatabase[i].currentEnrollment, courseDatabase[i].maxSeats);
                
                strncat(result, line, MSG_BUFFER_SIZE - strlen(result) - 1);
                
                int hasStudents = 0;
                for (int j = 0; j < enrollmentCount; j++) {
                    if (enrollmentDatabase[j].courseId == courseDatabase[i].id) {
                        User* student = getUserById(enrollmentDatabase[j].studentId);
                        
                        if (student) {
                            sprintf(line, "- %s (ID: %d)\n", student->username, student->id);
                            strncat(result, line, MSG_BUFFER_SIZE - strlen(result) - 1);
                            hasStudents = 1;
                        }
                    }
                }
                
                if (!hasStudents) {
                    strncat(result, "- No students enrolled yet\n", 
                            MSG_BUFFER_SIZE - strlen(result) - 1);
                }
                
                hasCourses = 1;
            }
        }
        
        releaseFileLock(COURSE_DB_FILE);
        releaseFileLock(ENROLLMENT_DB_FILE);
        
        if (!hasCourses) {
            strcpy(response, "You have not offered any courses");
        } else {
            strcpy(response, result);
        }
        
        free(result);
    }
    else if (strcmp(command, "VIEW_COURSES") == 0) {
        acquireReadLock(COURSE_DB_FILE);
        
        char* result = (char*)malloc(MSG_BUFFER_SIZE);
        strcpy(result, "Your courses:\n");
        
        int hasCourses = 0;
        for (int i = 0; i < courseCount; i++) {
            if (courseDatabase[i].facultyId == faculty->id) {
                char line[256];
                sprintf(line, "Code: %s, Name: %s, Enrollment: %d/%d\n", 
                        courseDatabase[i].code, courseDatabase[i].name, 
                        courseDatabase[i].currentEnrollment, courseDatabase[i].maxSeats);
                
                strncat(result, line, MSG_BUFFER_SIZE - strlen(result) - 1);
                hasCourses = 1;
            }
        }
        
        releaseFileLock(COURSE_DB_FILE);
        
        if (!hasCourses) {
            strcpy(response, "You have not offered any courses");
        } else {
            strcpy(response, result);
        }
        
        free(result);
    }
    else if (strcmp(command, "CHANGE_PASSWORD") == 0) {
        char* oldPassword = strtok(NULL, " ");
        char* newPassword = strtok(NULL, " ");
        
        if (!oldPassword || !newPassword) {
            strcpy(response, "Invalid format");
            return response;
        }
        
        if (strcmp(faculty->password, oldPassword) != 0) {
            strcpy(response, "Incorrect current password");
            return response;
        }
        
        strncpy(faculty->password, newPassword, STRING_MAX_LENGTH-1);
        faculty->password[STRING_MAX_LENGTH-1] = '\0';
        persistDatabase();
        
        strcpy(response, "Password changed successfully");
    }
    else {
        strcpy(response, "Invalid faculty command");
    }

    return response;
}

// Client connection handler thread function
void* clientConnectionHandler(void* client_socket) {
    int sock = *(int*)client_socket;
    free(client_socket);

    char buffer[MSG_BUFFER_SIZE] = {0};

    while (1) {
        // Clear the buffer
        memset(buffer, 0, MSG_BUFFER_SIZE);

        // Read client message
        ssize_t bytesRead = read(sock, buffer, MSG_BUFFER_SIZE - 1);
        if (bytesRead <= 0) {
            close(sock);
            printf("Client disconnected\n");
            return NULL;
        }

        // Process the request
        char* response = processClientRequest(buffer, sock);

        // Send response back to client
        send(sock, response, strlen(response), 0);
        free(response);

        // If client sent "EXIT", close the connection
        if (strcmp(buffer, "EXIT") == 0) {
            printf("Client requested to exit\n");
            close(sock);
            return NULL;
        }
    }

    return NULL;
}

// Main function
int main() {
    // Initialize semaphore
    sem_init(&accessMutex, 0, 1);
    
    // Set up signal handler
    signal(SIGINT, handleSignal);
    
    // Load data from files
    initializeDatabase();
    
    // Create a socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options to reuse address and port
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    // Server address configuration
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(SERVER_PORT);
    
    // Bind the socket to the specified port
    if (bind(serverSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Start listening for connections
    if (listen(serverSocket, CLIENT_MAX) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Academia Portal Server started on port %d\n", SERVER_PORT);
    printf("Waiting for connections...\n");
    
    // Accept and handle client connections
    while (1) {
        struct sockaddr_in clientAddress;
        socklen_t clientAddressLength = sizeof(clientAddress);
        
        // Accept a new client connection
        int* clientSocket = (int*)malloc(sizeof(int));
        *clientSocket = accept(serverSocket, (struct sockaddr *)&clientAddress, &clientAddressLength);
        
        if (*clientSocket < 0) {
            perror("Accept failed");
            free(clientSocket);
            continue;
        }
        
        // Get client IP address
        char clientIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddress.sin_addr), clientIp, INET_ADDRSTRLEN);
        printf("New connection from %s:%d\n", clientIp, ntohs(clientAddress.sin_port));
        
        // Create a new thread to handle the client
        pthread_t threadId;
        if (pthread_create(&threadId, NULL, clientConnectionHandler, (void*)clientSocket) != 0) {
            perror("Thread creation failed");
            close(*clientSocket);
            free(clientSocket);
            continue;
        }
        
        // Detach the thread to allow it to run independently
        pthread_detach(threadId);
    }
    
    // Clean up (unreachable due to signal handler)
    close(serverSocket);
    sem_destroy(&accessMutex);
    free(userDatabase);
    free(courseDatabase);
    free(enrollmentDatabase);
    
    return 0;
}