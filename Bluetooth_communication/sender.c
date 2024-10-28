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
    int client = args->socket;
    sqlite3 *db = args->db;
    char buf[1024];

    while (1) {
        printf("Enter message: ");
        fgets(buf, sizeof(buf), stdin);
        buf[strcspn(buf, "\n")] = 0; // Remove trailing newline character



        if (strlen(buf) == 0) {
            continue; // Ignore empty messages
        }

        if (write(client, buf, strlen(buf)) < 0) {
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
    int client = args->socket;
    sqlite3 *db = args->db;
    char buf[1024];

    while (1) {
        int bytes_read = read(client, buf, sizeof(buf) - 1);
        if (bytes_read > 0) {
            buf[bytes_read] = '\0'; // Null-terminate the string
            printf("\nReceived: %s\n", buf);
            store_message(db, "Received", buf); // Store received message
        } else if (bytes_read == 0) {
            printf("Client disconnected\n");
            break;
        } else {
            perror("Failed to read data");
            break;
        }
    }

    return NULL;
}

int main(int argc, char **argv) {
    struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
    char buf[1024] = { 0 };
    int s, client;
    socklen_t opt = sizeof(rem_addr);

    sqlite3 *db = init_database(); // Initialize the database

    // Allocate socket
    s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    // Bind socket to port 4 of the first available local bluetooth adapter
    loc_addr.rc_family = AF_BLUETOOTH;
    loc_addr.rc_bdaddr = *BDADDR_ANY;
    loc_addr.rc_channel = (uint8_t) 4;
    bind(s, (struct sockaddr *)&loc_addr, sizeof(loc_addr));

    // Put socket into listening mode
    listen(s, 1);

    // Accept one connection
    printf("Waiting for connection...\n");
    client = accept(s, (struct sockaddr *)&rem_addr, &opt);

    ba2str(&rem_addr.rc_bdaddr, buf);
    fprintf(stderr, "Accepted connection from %s\n", buf);
    memset(buf, 0, sizeof(buf));

    pthread_t send_thread, receive_thread;
    thread_args_t args = {client, db};

    pthread_create(&send_thread, NULL, send_messages, &args);
    pthread_create(&receive_thread, NULL, receive_messages, &args);

    pthread_join(send_thread, NULL);
    pthread_join(receive_thread, NULL);

    // Close connection
    close(client);
    close(s);
    sqlite3_close(db); // Close the database connection

    return 0;
}
