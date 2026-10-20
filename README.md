# HFT trade-lifecycle simulator

A compact C++20 project that models an exchange and an HFT market maker from
order entry to matching, execution reports, public market data, trader-side
book construction, signal generation, and pre-trade risk. Prices are integer
ticks, so matching and tests are deterministic.

## Architecture

    orders -> Exchange / price-time FIFO CLOB -> execution reports -> trader risk/P&L
                         |
                         +-> Add / Modify / Cancel / Trade events
                                      |
                                      v
                   sequenced, fixed-width UDP multicast feed
                                      |
                                      v
                  Local aggregated L2 book -> volatility -> market maker -> risk gate

The simulator uses the conventional market topology: an order-entry interface
is separate from the multicast market-data channel. In this implementation,
order entry and execution reports are direct C++ calls to keep the simulation
deterministic; exchange public data is also available over real UDP multicast.

## Implemented lifecycle

1. A limit, market, GTC, or IOC order is validated and accepted or rejected.
2. The central limit order book matches crossing orders at the passive
   (maker) price using strict price-time FIFO priority.
3. Each fill produces buyer and seller execution reports plus a public Trade
   event and a passive-order Modify or Cancel event.
4. A non-crossing GTC remainder rests and publishes an Add; unfilled
   market/IOC remainder is cancelled.
5. Cancels enforce trader and symbol ownership. Same-price reductions retain
   time priority; quantity increases and price changes reset it.

There are two complementary book implementations:

- exchange/OrderBook: an order-by-order, price-time matching CLOB. Price maps
  give best-price access, FIFO lists retain priority, and an order-id locator
  makes cancels efficient.
- market_data/LocalOrderBook: a trader-side aggregated L2 projection rebuilt
  from public events. It provides BBO, depth, and sequence-gap detection. A
  gap or malformed event marks it unsynchronized, preventing the trading
  engine from quoting until the caller resets and replays a recovered snapshot.

## Market data and network protocol

networking/MarketDataCodec encodes a fixed 60-byte, versioned HFT1,
network-byte-order datagram. It carries the sequence, timestamp, order ids,
price, quantity, symbol, side, and event type. The exchange can publish this
through UdpMulticastPublisher; UdpMulticastSubscriber joins the group and
decodes the feed. The demo group is 239.255.42.99:39001.

## Trading and risk

The example MarketMakerStrategy calculates a two-sided quote around the
midpoint. EWMA log-return volatility widens the spread, and inventory shifts
quotes to prefer reducing the current position.

RiskManager rejects orders that violate:

- kill switch, quantity, open-order count, or per-order notional limits;
- price collar relative to the current midpoint;
- worst-case position, gross notional, or daily realized-loss limits.

It also maintains position, average price, and realized P&L from execution
reports.

## Build and run

    cmake -S . -B build
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure
    ./build/hft_demo

Run the same lifecycle with the actual multicast publisher and a local
subscriber:

    ./build/hft_demo --udp

The tests cover maker-price matching, price-time priority, amend priority
rules, lifecycle feed ordering, binary codec round-trips, L2 gap detection,
risk/P&L, and EWMA volatility.

## Scope

This is intentionally a teaching/simulation system, not a production exchange.
A production implementation would add per-symbol sharding, concurrency and
lock-free queues, persistence/recovery, authenticated order-entry transport,
drop-copy, snapshots/retransmission, clock synchronization, and stronger
overflow/audit controls.
