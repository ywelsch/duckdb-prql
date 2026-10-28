#pragma once

#include "duckdb.hpp"
#include "duckdb/parser/parser_extension.hpp"
#include "duckdb/planner/operator_extension.hpp"

namespace duckdb {

class PrqlExtension : public Extension {
public:
  void Load(ExtensionLoader &loader) override;
  std::string Name() override { return "prql"; }
};

BoundStatement prql_bind(ClientContext &context, Binder &binder,
                         OperatorExtensionInfo *info, SQLStatement &statement);

struct PrqlOperatorExtension : public OperatorExtension {
  PrqlOperatorExtension() : OperatorExtension() { Bind = prql_bind; }

  std::string GetName() override { return "prql"; }

  unique_ptr<LogicalExtensionOperator>
  Deserialize(Deserializer &deserializer) override {
    throw InternalException("prql operator should not be serialized");
  }
};

ParserExtensionParseResult prql_parse(ParserExtensionInfo *,
                                      const vector<SimpleToken> &tokens);

ParserExtensionPlanResult prql_plan(ParserExtensionInfo *, ClientContext &,
                                    unique_ptr<ParserExtensionParseData>);

struct PrqlParserExtension : public ParserExtension {
  PrqlParserExtension() : ParserExtension() {
    parse_function = prql_parse;
    plan_function = prql_plan;
  }
};

// Marker for statements claimed by prql_parse. The parser extension only gets
// to see tokens (without whitespace), so the PRQL is compiled in prql_bind,
// based on the statement's source text.
struct PrqlParseData : ParserExtensionParseData {
  unique_ptr<ParserExtensionParseData> Copy() const override {
    return make_uniq_base<ParserExtensionParseData, PrqlParseData>();
  }

  virtual string ToString() const override { return "PrqlParseData"; }
};

// Keeps the SQL statement compiled from PRQL alive until the end of the query
class PrqlState : public ClientContextState {
public:
  explicit PrqlState(unique_ptr<SQLStatement> statement)
      : statement(std::move(statement)) {}

  void QueryEnd() override { statement.reset(); }

  unique_ptr<SQLStatement> statement;
};

} // namespace duckdb
