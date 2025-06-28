CREATE TABLE Wallets (chat_id INTEGER PRIMARY KEY, time_zone TEXT);

CREATE TABLE WalletEntries(
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    chat_id INTEGER,
    ts INTEGER,
    amount REAL,
    descr TEXT
);

CREATE INDEX WalletEntriesChatIdIndex ON WalletEntries(chat_id);

CREATE INDEX WalletEntriesTsIndex ON WalletEntries(ts);
