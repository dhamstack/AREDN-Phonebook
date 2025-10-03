# SIP User Agent Client (UAC) - Artificial Phone for Load Testing

## Project Plan: Building the Artificial SIP Phone (UAC)

The goal is to build a high-performance **Artificial Phone** right alongside our **SIP Server** on the same machine. This phone's job is to make simultaneous calls to real network phones to measure how well our server handles the load.

This uac has to be integrated into the existing phonebook code

---

## 1. The Core Job: Making and Breaking Calls (Functional Goals)

### 1.1 Target Selection from Registered Users

The UAC must obtain phone numbers from the existing phonebook's registered users database:

- **Data Source:** Access the global `registered_users[]` array (defined in `common.h`)
- **Target Criteria:** Select only active users (`is_active == true`)
- **Thread Safety:** Must lock `registered_users_mutex` before accessing the array
- **Phone Numbers:** Available in the `user_id` field of `RegisteredUser` struct
- **User Types:**
  - Directory users (`is_known_from_directory == true`) - from CSV phonebook
  - Dynamic registrations (`is_known_from_directory == false`) - from SIP REGISTER

**Access Pattern:**
```c
pthread_mutex_lock(&registered_users_mutex);
for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
    if (registered_users[i].user_id[0] != '\0' &&
        registered_users[i].is_active) {
        // Use registered_users[i].user_id as target phone number
    }
}
pthread_mutex_unlock(&registered_users_mutex);
```

### 1.2 UAC Identity

The artificial phone must identify itself with a reserved phone number:

- **UAC Phone Number:** 999900 (reserved for load testing)
- **Usage:** This number appears in From/Contact headers of all SIP messages
- **Purpose:** Ensures the UAC is identifiable and doesn't conflict with real user numbers

### 1.3 Call Establishment Requirements

Our artificial phone needs to be a fully capable, if fast-talking, SIP client.

- **Make the Call (INVITE):** The phone must kick off a call session by sending an INVITE to the SIP Server on its standard port (5060).
- **Handle the Response Chain:** It needs to listen for and correctly process all the standard responses (100 Trying, 180 Ringing, 200 OK).
- **Seal the Deal (ACK):** Once it gets the final 200 OK response, it must immediately send an ACK to confirm the session is established and ready for media.
- **Simulate the Conversation (RTP):** This is where the load happens. When the call is active, the phone must start sending realistic RTP (media) packets to the real phone to simulate audio traffic and stress the network link.
- **Hang Up (BYE):** It must be able to gracefully end the call by sending a BYE request.
- **Describe Itself (SDP):** The INVITE must contain an SDP block that tells the real phone *which* media ports to send its audio back to.

---

## 2. The Tricky Part: Port Management on a Single Host (Networking Rules)

Since the SIP server is already using 5060, the artificial phone needs to be a good neighbor and pick its own unique ports.

| Area | Requirement | Why It Matters |
|------|-------------|----------------|
| **SIP Signaling Port** | The artificial phone **must bind to a unique local port** (e.g., 5070, or any available high port). It cannot use 5060. | If we use 5060, the server will crash or the phone won't be able to start. |
| **Advertising Its Address (Via Header)** | Every single message the phone sends (INVITE, BYE) must include a Via header that explicitly lists its unique local port (e.g., `Via: ...:5070`). | This is the GPS for the SIP server. Without it, the server won't know where to send its *responses* (200 OK) back to the phone. |
| **Media Ports (RTP/RTCP)** | **Every single concurrent call** must use a unique, non-overlapping pair of UDP ports for the media traffic. | If two calls use the same RTP port, the data gets scrambled, and the test is invalid. |
| **RTP Port Pool** | We need a dedicated system to **reserve and release RTP port pairs** (e.g., from 10000 upwards) to ensure uniqueness across all concurrent calls. | This prevents us from accidentally reusing a port while the previous call is still active. |

---

## 3. How It Needs to Be Built (Design and Performance)

This isn't just a simple script; it has to be robust enough to flood the system.

- **Concurrency is Key:** The phone must be designed to manage **hundreds of calls simultaneously**. We need a multithreaded or asynchronous architecture to handle the independent state and media traffic for each call without blocking the others.
- **State Machine:** We must build a clean, reliable SIP **State Machine** for each call instance (e.g., *Idle* → *Calling* → *In-Call* → *Disconnected*). This prevents calls from getting stuck.
- **Measurement:** The primary output is data. The code must **time critical events** (call setup latency, media start time) to give us the performance metrics we need.
- **Configuration:** We need an easy way to **configure the load**, meaning we can dial up or dial down the number of simultaneous calls being made.

This approach ensures the phone works correctly alongside the server while generating the controlled, measurable traffic needed for performance analysis.