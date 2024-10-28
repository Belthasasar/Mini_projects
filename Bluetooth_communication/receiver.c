#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <sqlite3.h>

// Function to initialize the SQLite database
sqlite3* init_database() {
    sqlite3 *db;
    char *errMsg = 0;
    int rc = sqlite3_open("messages.db", &db);

    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        exit(1);
    }

    const char *sql = "CREATE TABLE IF NOT EXISTS MESSAGES("  \
                      "ID INTEGER PRIMARY KEY AUTOINCREMENT," \
                      "DIRECTION TEXT NOT NULL," \
                      "MESSAGE TEXT NOT NULL);";

    rc = sqlite3_exec(db, sql, 0, 0, &errMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", errMsg);
        sqlite3_free(errMsg);
        exit(1);
    }

    return db;
}

// Function to store a message in the database
void store_message(sqlite3 *db, const char *direction, const char *message) {
    char *errMsg = 0;
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO MESSAGES (DIRECTION, MESSAGE) VALUES (?, ?);";

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return;
    }

    sqlite3_bind_text(stmt, 1, direction, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, message, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Failed to execute statement: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
}

typedef struct {
    int socket;
    sqlite3 *db;
} thread_args_t;

void* send_messages(void* arg) {
    thread_args_t* args = (thread_args_t*) arg;
    int s = args->socket;
    sqlite3 *db = args->db;
    char buf[1024];

    while (1) {
        printf("Enter message: ");
        fgets(buf, sizeof(buf), stdin);
        buf[strcspn(buf, "\n")] = 0; // Remove trailing newline character

        if (strlen(buf) == 0) {
            continue; // Ignore empty messages
        }

        if (write(s, buf, strlen(buf)) < 0) {
            perror("Failed to send message");
            break;
        }

        store_message(db, "Sent", buf); // Store sent message

        // Clear the buffer
        memset(buf, 0, sizeof(buf));
    }

    return NULL;
}

void* receive_messages(void* arg) {
    thread_args_t* args = (thread_args_t*) arg;
    int s = args->socket;
    sqlite3 *db = args->db;
    char buf[1024];

    while (1) {
        int bytes_read = read(s, buf, sizeof(buf) - 1);
        if (bytes_read > 0) {
            buf[bytes_read] = '\0'; // Null-terminate the string
            printf("Received: %s \n", buf);
            store_message(db, "Received", buf); // Store received message
        } else if (bytes_read == 0) {
            printf("Server disconnected\n");
            break;
        } else {
            perror("Failed to read data");
            break;
        }
    }

    return NULL;
}

int main(int argc, char **argv) {
    struct sockaddr_rc addr = { 0 };
    int s, status;
    char dest[18] = "60:E9:AA:46:FE:B4"; // Replace with the server's MAC address

    sqlite3 *db = init_database(); // Initialize the database

    // Allocate socket
    s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    // Set the connection parameters (who to connect to)
    addr.rc_family = AF_BLUETOOTH;
    str2ba(dest, &addr.rc_bdaddr);
    addr.rc_channel = (uint8_t) 4;

    // Connect to server
    status = connect(s, (struct sockaddr *)&addr, sizeof(addr));

    if (status == 0) {
        printf("Connected!\n");

        pthread_t send_thread, receive_thread;
        thread_args_t args = {s, db};

        pthread_create(&send_thread, NULL, send_messages, &args);
        pthread_create(&receive_thread, NULL, receive_messages, &args);

        pthread_join(send_thread, NULL);
        pthread_join(receive_thread, NULL);
    } else {
        perror("Failed to connect");
    }

    // Close connection
    close(s);
    sqlite3_close(db); // Close the database connection
    printf("Disconnected\n");

    return 0;
}

