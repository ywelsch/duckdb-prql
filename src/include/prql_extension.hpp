#pragma once

#include "duckdb.hpp"

namespace duckdb {

class PrqlExtension : public Extension {
public:
  void Load(DuckDB &db) override;
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
                                      const std::string &query);

ParserExtensionPlanResult prql_plan(ParserExtensionInfo *, ClientContext &,
                                    unique_ptr<ParserExtensionParseData>);

struct PrqlParserExtension : public ParserExtension {
  PrqlParserExtension() : ParserExtension() {
    parse_function = prql_parse;
    plan_function = prql_plan;
  }
};

struct PrqlParseData : ParserExtensionParseData {
  unique_ptr<SQLStatement> statement;

  unique_ptr<ParserExtensionParseData> Copy() const override {
    return make_uniq_base<ParserExtensionParseData, PrqlParseData>(
        statement->Copy());
  }

  virtual string ToString() const override { return "PrqlParseData"; }

  PrqlParseData(unique_ptr<SQLStatement> statement)
      : statement(std::move(statement)) {}
};

class PrqlState : public ClientContextState {
public:
  explicit PrqlState(unique_ptr<ParserExtensionParseData> parse_data)
      : parse_data(std::move(parse_data)) {}

  void QueryEnd() override { parse_data.reset(); }

  unique_ptr<ParserExtensionParseData> parse_data;
};

} // namespace duckdb
