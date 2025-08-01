# Emptying a table

<!-- example truncate -->

The table can be emptied with a `TRUNCATE TABLE` SQL statement or with a `truncate()` PHP client function.

Here is the syntax for the SQL statement:

```sql
TRUNCATE TABLE table_name [WITH RECONFIGURE]
```

When this statement is executed, it clears the RT or distributed table completely. It disposes the in-memory data, unlinks all the table data files, and releases the associated binary logs.

For emptying a distributed table, use syntax without the `with reconfigure` option. Simply execute the standard TRUNCATE statement against your distributed table.

```sql
TRUNCATE TABLE distributed_table
```

> NOTE: Emptying a distributed table requires [Manticore Buddy](../Installation/Manticore_Buddy.md). If it doesn't work, make sure Buddy is installed.

A table can also be emptied with `DELETE FROM index WHERE id>0`, but it's not recommended as it's slower than `TRUNCATE`.

<!-- intro -->
##### SQL:
<!-- request SQL -->

```sql
TRUNCATE TABLE products;
```
<!-- response SQL -->

```sql
Query OK, 0 rows affected (0.02 sec)
```

<!-- intro -->
##### JSON:

<!-- request JSON -->

```http
POST /cli -d "TRUNCATE TABLE products"
```

<!-- response JSON -->
```http
{
"total":0,
"error":"",
"warning":""
}
```

<!-- intro -->
##### PHP:

<!-- request PHP -->

```php
$params = [ 'table' => 'products' ];
$response = $client->indices()->truncate($params);
```

<!-- response PHP -->
```php
Array(
    [total] => 0
    [error] =>
    [warning] =>
)
```
<!-- intro -->
##### Python:

<!-- request Python -->

```python
utilsApi.sql('TRUNCATE TABLE products')
```

<!-- response Python -->
```python
{u'error': u'', u'total': 0, u'warning': u''}
```

<!-- intro -->
##### Python-asyncio:

<!-- request Python-asyncio -->

```python
await utilsApi.sql('TRUNCATE TABLE products')
```

<!-- response Python-asyncio -->
```python
{u'error': u'', u'total': 0, u'warning': u''}
```

<!-- intro -->
##### Javascript:

<!-- request javascript -->

```javascript
res = await utilsApi.sql('TRUNCATE TABLE products');
```

<!-- response javascript -->
```javascript
{"total":0,"error":"","warning":""}
```

<!-- intro -->
##### java:

<!-- request Java -->

```java
utilsApi.sql("TRUNCATE TABLE products", true);
```

<!-- response Java -->
```java
{total=0, error=, warning=}
```

<!-- intro -->
##### C#:

<!-- request C# -->

```clike
utilsApi.Sql("TRUNCATE TABLE products", true);
```

<!-- response C# -->
```clike
{total=0, error="", warning=""}
```

<!-- intro -->
##### Rust:

<!-- request Rust -->

```rust
utils_api.sql("TRUNCATE TABLE products", Some(true)).await;
```

<!-- response Rust -->
```rust
{total=0, error="", warning=""}
```

<!-- end -->

One of the possible uses of this command is before [attaching a table](Data_creation_and_modification/Adding_data_from_external_storages/Adding_data_to_tables/Attaching_one_table_to_another.md).

<!-- example truncate with RECONFIGURE -->

When `RECONFIGURE` option is used new tokenization, morphology, and other text processing settings specified in the config take effect after the table gets cleared. In case the [schema declaration](Creating_a_table/Data_types.md) in config is different from the table schema the new schema from config got applied after table get cleared.

> NOTE: The `RECONFIGURE` option only makes sense in [Plain mode](Read_this_first.md#Real-time-mode-vs-plain-mode), where it applies the settings from the configuration file. Note that `TRUNCATE` is only supported for RT tables, and the `RECONFIGURE` option can only be used with RT tables when Manticore is running in Plain mode.

With this option clearing and reconfiguring a table becomes one atomic operation.

<!-- intro -->
##### SQL:
<!-- request SQL -->

```sql
TRUNCATE TABLE products with reconfigure;
```
<!-- response SQL -->

```sql
Query OK, 0 rows affected (0.02 sec)
```

<!-- intro -->
##### JSON:

<!-- request HTTP -->

```http
POST /cli -d "TRUNCATE TABLE products with reconfigure"
```

<!-- response HTTP -->
```http
{
"total":0,
"error":"",
"warning":""
}
```

<!-- intro -->
##### PHP:

<!-- request PHP -->

```php
$params = [ 'table' => 'products', 'with' => 'reconfigure' ];
$response = $client->indices()->truncate($params);
```

<!-- response PHP -->
```php
Array(
    [total] => 0
    [error] =>
    [warning] =>
)
```
<!-- intro -->
##### Python:

<!-- request Python -->

```python
utilsApi.sql('TRUNCATE TABLE products WITH RECONFIGURE')
```

<!-- response Python -->
```python
{u'error': u'', u'total': 0, u'warning': u''}
```

<!-- intro -->
##### Python-asyncio:

<!-- request Python-asyncio -->

```python
await utilsApi.sql('TRUNCATE TABLE products WITH RECONFIGURE')
```

<!-- response Python-asyncio -->
```python
{u'error': u'', u'total': 0, u'warning': u''}
```

<!-- intro -->
##### Javascript:

<!-- request javascript -->

```javascript
res = await utilsApi.sql('TRUNCATE TABLE products WITH RECONFIGURE');
```

<!-- response javascript -->
```javascript
{"total":0,"error":"","warning":""}
```

<!-- intro -->
##### java:

<!-- request Java -->

```java
utilsApi.sql("TRUNCATE TABLE products WITH RECONFIGURE", true);
```

<!-- response Java -->
```java
{total=0, error=, warning=}
```

<!-- intro -->
##### C#:

<!-- request C# -->

```clike
utilsApi.Sql("TRUNCATE TABLE products WITH RECONFIGURE" ,true);
```

<!-- response C# -->
```clike
{total=0, error="", warning=""}
```

<!-- intro -->
##### Rust:

<!-- request Rust -->

```rust
utils_api.sql("TRUNCATE TABLE products WITH RECONFIGURE", Some(true)).await;
```

<!-- response C# -->
```clike
{total=0, error="", warning=""}
```

<!-- end -->
<!-- proofread -->
