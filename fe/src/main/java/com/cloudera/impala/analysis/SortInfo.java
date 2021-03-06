// Copyright (c) 2012 Cloudera, Inc. All rights reserved.

package com.cloudera.impala.analysis;

import java.util.List;

/**
 * Encapsulates all the information needed to compute ORDER BY
 * This doesn't contain aliases or positional exprs.
 */
public class SortInfo {
  private final List<Expr> orderingExprs;
  private final List<Boolean> isAscOrder;

  public SortInfo(List<Expr> orderingExprs, List<Boolean> isAscOrder) {
    this.orderingExprs = orderingExprs;
    this.isAscOrder = isAscOrder;
  }

  public List<Expr> getOrderingExprs() {
    return orderingExprs;
  }

  public List<Boolean> getIsAscOrder() {
    return isAscOrder;
  }

  /**
   * Substitute all the ordering expression according to the substitution map.
   * @param sMap
   */
  public void substitute(Expr.SubstitutionMap sMap) {
    Expr.substituteList(orderingExprs, sMap);
  }
}

