# PRQL as a DuckDB extension

Extension to [DuckDB](https://duckdb.org) that allows running [PRQL](https://prql-lang.org) commands directly within DuckDB.

## Running the extension

For installation instructions, see further below. After installing the extension, you can directly query DuckDB using PRQL, the Piped Relational Query Language. Both PRQL and SQL commands are supported within the same shell.

As PRQL does not support DDL commands, we use SQL for defining our tables:

```sql
INSTALL httpfs;
LOAD httpfs;
CREATE TABLE invoices AS SELECT * FROM
  read_csv_auto('https://raw.githubusercontent.com/PRQL/prql/main/prql-compiler/tests/integration/data/chinook/invoices.csv');
CREATE TABLE customers AS SELECT * FROM
  read_csv_auto('https://raw.githubusercontent.com/PRQL/prql/main/prql-compiler/tests/integration/data/chinook/customers.csv');
```

and finally query using PRQL:

```elm
from invoices
filter invoice_date >= @1970-01-16
derive [
  transaction_fees = 0.8,
  income = total - transaction_fees
]
filter income > 1
group customer_id (
  aggregate [
    average total,
    sum_income = sum income,
    ct = count,
  ]
)
sort [-sum_income]
take 10
join c=customers [==customer_id]
derive name = f"{c.last_name}, {c.first_name}"
select [
  c.customer_id, name, sum_income
]
derive db_version = s"version()";
```

which returns:

```
┌─────────────┬─────────────────────┬────────────┬────────────┐
│ customer_id │        name         │ sum_income │ db_version │
│    int64    │       varchar       │   double   │  varchar   │
├─────────────┼─────────────────────┼────────────┼────────────┤
│           6 │ Holý, Helena        │      43.83 │ v0.7.0     │
│           7 │ Gruber, Astrid      │      36.83 │ v0.7.0     │
│          24 │ Ralston, Frank      │      37.83 │ v0.7.0     │
│          25 │ Stevens, Victor     │      36.83 │ v0.7.0     │
│          26 │ Cunningham, Richard │      41.83 │ v0.7.0     │
│          28 │ Barnett, Julia      │      37.83 │ v0.7.0     │
│          37 │ Zimmermann, Fynn    │      37.83 │ v0.7.0     │
│          45 │ Kovács, Ladislav    │      39.83 │ v0.7.0     │
│          46 │ O'Reilly, Hugh      │      39.83 │ v0.7.0     │
│          57 │ Rojas, Luis         │      40.83 │ v0.7.0     │
├─────────────┴─────────────────────┴────────────┴────────────┤
│ 10 rows                                           4 columns │
└─────────────────────────────────────────────────────────────┘
```

## Install

To install the PRQL extension, DuckDB needs to be launched with the `allow_unsigned_extensions` option set to true.
Depending on the DuckDB usage, this can be configured as follows:

CLI:
```shell
duckdb -unsigned
```

Python:
```python
con = duckdb.connect(':memory:', config={'allow_unsigned_extensions' : 'true'})
```

A custom extension repository then needs to be defined as follows:
```sql
SET custom_extension_repository='welsch.lu/duckdb/prql/latest';
```
Note that the `/latest` path will provide the latest extension version available for the current version of DuckDB.
A given extension version can be selected by using that version as last path element instead.

After running these steps, the extension can then be installed and loaded using the regular INSTALL/LOAD commands in DuckDB:
```sql
FORCE INSTALL prql; # To override current installation with latest
LOAD prql;
```

## Build from source
To build the extension:
```sh
make
```
The main binaries that will be built are:
```sh
./build/release/duckdb
./build/release/test/unittest
./build/release/extension/prql/prql.duckdb_extension
```
- `duckdb` is the binary for the duckdb shell with the extension code automatically loaded.
- `unittest` is the test runner of duckdb. Again, the extension is already linked into the binary.
- `prql.duckdb_extension` is the loadable binary as it would be distributed.

To run the extension code, simply start the shell with `./build/release/duckdb`.
